#include <bloom/bloom.h>

#include <BNM/BasicMonoStructures.hpp>
#include <BNM/Class.hpp>
#include <BNM/Field.hpp>
#include <BNM/Method.hpp>
#include <BNM/MethodBase.hpp>

#include <dlfcn.h>

#include <map>
#include <mutex>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "log.h"

// Tutor v5. A conjure becomes a tutor: it may only pick cards that are in the
// player's deck (the game's own subset query does the filtering).
//
// Which abilities tutor is decided ONLY by card data: an ability is a tutor
// when its SubsetQuery is a CompositeAllQuery that directly contains the marker
//     NotQuery(HasComponentQuery(PvZCards.Engine.Components.Deck))
// No card has a Deck component, so the marker never changes what matches.
//
// config.ini only controls how a tutor behaves:
//   Fizzle=true     no match in the deck: the effect does nothing
//                   (false: conjure normally instead).
//   Delivery=draw   the picked card is moved to the top of the deck and drawn
//                   with the game's own "draw a card" code (counts as drawn,
//                   not conjured). Delivery=conjure creates the card and then
//                   removes one copy from the deck.

using Obj = BNM::IL2CPP::Il2CppObject;
template<typename T> using MonoList = BNM::Structures::Mono::List<T>;

static BNM::Field<int> effectSource, effectPlayer;                  // Effect
static BNM::Field<MonoList<Obj *> *> modelEntities;                 // EntityModel.Entities
static BNM::Field<MonoList<Obj *> *> entityComponents;              // Entity.Components
static BNM::Field<int> entityId;                                    // Entity.Id
static BNM::Field<Obj *> subsetSystemModel, subsetSystemFinder;     // DrawCardFromSubsetSystem
static BNM::Field<Obj *> drawSystemModel;                           // DrawCardSystem.Entities
static BNM::Field<Obj *> abilitySystemModel;                        // DrawCardAbilitySystem.EntityModel
static BNM::Field<Obj *> subsetEffectQuery;                         // DrawCardFromSubsetEffect.SubsetQuery
static BNM::Field<MonoList<Obj *> *> compositeQueries;              // CompositeQuery.Queries
static BNM::Field<Obj *> notQueryInner;                             // NotQuery.Query
static BNM::Field<BNM::MonoType *> hasComponentType;                // HasComponentQuery.ComponentType
static BNM::Method<int> finderFindCard;                             // CardFromSubsetFinder.FindCard(query, effect)
static BNM::Method<void> drawCardsFromDeck;                         // DrawCardAbilitySystem.DrawCardsFromDeck
static BNM::Method<void> effectCancel;                              // Effect.Cancel
static BNM::Method<int> subsetHandId, subsetDrawnId;                // DrawCardFromSubsetEffect getters
static BNM::Method<int> drawHandId, drawDeckId;                     // DrawCardEffect getters
static BNM::Field<int> cardGuid;                                    // Card.Guid
static BNM::Field<MonoList<Obj *> *> poolEntries;                   // OrderedCardPool.Entries
static BNM::Field<int> entryGuid, entryEntityId;                    // CardPoolEntry

static bool verbose = false, fizzle = true, deliverByDraw = true;

// IL2CPP weak GC handles: remember DrawCardAbilitySystem instances without
// keeping old matches alive or touching freed memory.
static uintptr_t (*gcNewWeak)(Obj *, bool) = nullptr;
static Obj *(*gcTarget)(uintptr_t) = nullptr;
static void (*gcFree)(uintptr_t) = nullptr;

static std::mutex stateMutex;
// Entity ids are the same in the real game and in the AI's simulated copies.
static std::map<int, int> deckForHand, deckForPlayer;
static std::vector<uintptr_t> abilitySystems;                        // weak handles
// Deck entries moved to the top for a queued tutor draw, per (model, deck).
// Checked against the live list on every use, so entries that were drawn, or
// a deck the AI reset between simulations, clear themselves.
static std::map<std::pair<Obj *, int>, std::vector<Obj *>> reservedEntries;

struct TutorState {
    std::multiset<int> deckGuids;
    bool precheck = false;
    bool pickCalled = false;
    bool noMatch = false;
    int pickedGuid = -1;
};
static thread_local TutorState *current = nullptr;

static void (*originalProcessSubset)(void *, Obj *, const void *) = nullptr;
static void (*originalProcessDraw)(void *, Obj *, const void *) = nullptr;
static int (*originalPickRandomGuid)(void *, Obj *, const void *) = nullptr;
static void (*originalRegisterHandlers)(void *, Obj *, const void *) = nullptr;

template<typename T> static std::vector<T> Items(MonoList<T> *list) {
    std::vector<T> out;
    if (!list || !list->items) return out;
    for (int i = 0; i < list->size; i++) out.push_back(list->items->m_Items[i]);
    return out;
}

static std::string TypeName(Obj *object) {
    if (!object) return "null";
    auto *klass = BNM::Class(object).GetClass();
    return klass && klass->name ? klass->name : "?";
}

static Obj *FindComponent(Obj *entity, const char *name) {
    if (!entity) return nullptr;
    for (auto *component : Items(entityComponents[entity].Get()))
        if (TypeName(component) == name) return component;
    return nullptr;
}

static Obj *FindEntity(Obj *model, int id) {
    if (!model) return nullptr;
    for (auto *entity : Items(modelEntities[model].Get()))
        if (entity && entityId[entity].Get() == id) return entity;
    return nullptr;
}

static int GuidOf(Obj *entity) {
    auto *card = FindComponent(entity, "Card");
    return card ? cardGuid[card].Get() : -1;
}

// True if this query (a CompositeAllQuery, possibly nested) contains the tutor marker.
static bool HasTutorMarker(Obj *query, int depth = 0) {
    if (!query || depth > 8) return false;
    const auto name = TypeName(query);
    if (name == "CompositeAllQuery") {
        for (auto *inner : Items(compositeQueries[query].Get()))
            if (TypeName(inner) == "CompositeAllQuery" ? HasTutorMarker(inner, depth + 1) : HasTutorMarker(inner, -1)) return true;
        return false;
    }
    // Only a direct member of a CompositeAllQuery counts (depth -1): inside an
    // "any" or "not" query the marker would change what matches.
    if (depth != -1 || name != "NotQuery") return false;
    Obj *inner = notQueryInner[query].Get();
    if (TypeName(inner) != "HasComponentQuery") return false;
    auto *type = hasComponentType[inner].Get();
    auto *klass = type ? BNM::Class(type).GetClass() : nullptr;
    return klass && klass->name && klass->namespaze && std::string(klass->name) == "Deck" &&
           std::string(klass->namespaze) == "PvZCards.Engine.Components";
}

static int RandomIndex(size_t count) {
    static thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_int_distribution<int>(0, (int) count - 1)(rng);
}

// Reserved entries still present in the deck; forgets the rest.
static std::set<Obj *> Reserved(Obj *model, int deck, const std::vector<Obj *> &entries) {
    std::lock_guard lock(stateMutex);
    std::set<Obj *> present;
    auto it = reservedEntries.find({model, deck});
    if (it == reservedEntries.end()) return present;
    const std::set<Obj *> live(entries.begin(), entries.end());
    std::vector<Obj *> keep;
    for (auto *entry : it->second)
        if (live.count(entry)) { keep.push_back(entry); present.insert(entry); }
    if (keep.empty()) reservedEntries.erase(it);
    else it->second.swap(keep);
    return present;
}

// Remember every DrawCardAbilitySystem as it is set up for a match.
static void RegisterHandlers(void *self, Obj *router, const void *method) {
    originalRegisterHandlers(self, router, method);
    if (!self || !gcNewWeak || !gcTarget || !gcFree) return;
    std::lock_guard lock(stateMutex);
    std::vector<uintptr_t> alive;
    for (auto handle : abilitySystems) {
        if (gcTarget(handle)) alive.push_back(handle);
        else gcFree(handle);
    }
    alive.push_back(gcNewWeak((Obj *) self, false));
    abilitySystems.swap(alive);
    if (verbose) LOG_INFO("Tutor: DrawCardAbilitySystem registered (%zu tracked)", abilitySystems.size());
}

static Obj *AbilitySystemFor(Obj *model) {
    if (!gcTarget) return nullptr;
    std::lock_guard lock(stateMutex);
    for (auto handle : abilitySystems)
        if (Obj *system = gcTarget(handle); system && abilitySystemModel[system].Get() == model) return system;
    return nullptr;
}

// Remember which deck feeds which hand. Opening-hand draws happen before any tutor.
static void ProcessDraw(void *self, Obj *effect, const void *method) {
    if (effect) {
        const int deck = drawDeckId[effect]();
        std::lock_guard lock(stateMutex);
        deckForHand[drawHandId[effect]()] = deck;
        deckForPlayer[effectPlayer[effect].Get()] = deck;
    }
    originalProcessDraw(self, effect, method);
}

// Called with every card matching the query; returns the chosen guid.
static int PickRandomGuid(void *self, Obj *cards, const void *method) {
    if (!current) return originalPickRandomGuid(self, cards, method);
    current->pickCalled = true;
    if (current->pickedGuid >= 0) return current->pickedGuid;  // chosen during the pre-check
    if (TypeName(cards) != "List`1") {
        LOG_WARN("Tutor: candidate list is %s, not a List; conjuring normally", TypeName(cards).c_str());
        return originalPickRandomGuid(self, cards, method);
    }
    std::vector<int> matches;
    const auto candidates = Items(reinterpret_cast<MonoList<Obj *> *>(cards));
    for (auto *card : candidates) {
        const int guid = GuidOf(card);
        for (size_t n = current->deckGuids.count(guid); n > 0; n--) matches.push_back(guid);  // weight by copies
    }
    if (matches.empty()) {
        current->noMatch = true;
        if (current->precheck) return -1;  // result discarded; the game's RNG stays untouched
        if (verbose) LOG_INFO("Tutor: none of %zu candidates are in the deck; conjuring normally", candidates.size());
        return originalPickRandomGuid(self, cards, method);
    }
    current->pickedGuid = matches[RandomIndex(matches.size())];
    if (verbose) LOG_INFO("Tutor: %zu of %zu candidate copies are in the deck; picked guid %d",
                          matches.size(), candidates.size(), current->pickedGuid);
    return current->pickedGuid;
}

// Index of a deck entry with this guid, skipping reserved entries; prefers plain entries.
static int FindEntry(const std::vector<Obj *> &entries, int guid, const std::set<Obj *> &skip) {
    int index = -1;
    for (int i = 0; i < (int) entries.size(); i++) {
        auto *entry = entries[i];
        if (!entry || skip.count(entry) || entryGuid[entry].Get() != guid) continue;
        if (index < 0) index = i;
        if (entryEntityId[entry].Get() <= 0) return i;
    }
    return index;
}

// Managed List methods keep the GC informed of the change.
static BNM::Method<void> ListMethod(MonoList<Obj *> *list, const char *name, int parameters) {
    auto method = BNM::Class((Obj *) list).GetMethod(name, parameters);
    if (!method.IsValid()) LOG_ERR("Tutor: List.%s not found", name);
    return method;
}

// Moves entry `index` to the top of the deck (index 0). Checks both methods before changing anything.
static bool MoveToTop(MonoList<Obj *> *list, int index, Obj *entry) {
    auto removeAt = ListMethod(list, "RemoveAt", 1);
    auto insert = ListMethod(list, "Insert", 2);
    if (!removeAt.IsValid() || !insert.IsValid()) return false;
    removeAt[(Obj *) list](index);
    insert[(Obj *) list](0, entry);
    return true;
}

static bool RemoveEntry(MonoList<Obj *> *list, int index) {
    auto removeAt = ListMethod(list, "RemoveAt", 1);
    if (!removeAt.IsValid()) return false;
    removeAt[(Obj *) list](index);
    return true;
}

static void ProcessSubset(void *self, Obj *effect, const void *method) {
    // Only abilities whose card data carries the marker are tutors; everything else is untouched.
    if (!self || !effect || !HasTutorMarker(subsetEffectQuery[effect].Get())) {
        originalProcessSubset(self, effect, method);
        return;
    }
    Obj *model = subsetSystemModel[(Obj *) self].Get();
    const int sourceGuid = GuidOf(FindEntity(model, effectSource[effect].Get()));  // for the log

    const int hand = subsetHandId[effect](), player = effectPlayer[effect].Get();
    int deck = -1;
    {
        std::lock_guard lock(stateMutex);
        if (auto it = deckForHand.find(hand); it != deckForHand.end()) deck = it->second;
        else if (auto it2 = deckForPlayer.find(player); it2 != deckForPlayer.end()) deck = it2->second;
    }
    auto *deckEntity = FindEntity(model, deck);
    auto *pool = FindComponent(deckEntity, "OrderedCardPool");
    auto *list = pool ? poolEntries[pool].Get() : nullptr;
    if (!list) {
        LOG_WARN("Tutor: no deck for hand %d / player %d (deck %d); conjuring normally", hand, player, deck);
        originalProcessSubset(self, effect, method);
        return;
    }

    // Cards already reserved on top for a pending tutor draw are not available again.
    TutorState state;
    const auto entries = Items(list);
    const auto reserved = Reserved(model, deck, entries);
    for (auto *entry : entries)
        if (entry && !reserved.count(entry)) state.deckGuids.insert(entryGuid[entry].Get());

    TutorState *previous = current;
    current = &state;

    // Pre-check: run the game's finder once to see whether anything in the deck matches.
    Obj *finder = subsetSystemFinder[(Obj *) self].Get();
    Obj *query = subsetEffectQuery[effect].Get();
    if (finder && query) {
        state.precheck = true;
        finderFindCard[finder](query, effect);
        state.precheck = false;
        state.pickCalled = false;
    } else {
        LOG_WARN("Tutor: pre-check unavailable (finder %p, query %p)", finder, query);
    }

    if (state.noMatch && fizzle) {
        current = previous;
        effectCancel[effect]();
        LOG_INFO("Tutor: [model %p] source guid %d found nothing in deck %d; fizzled", model, sourceGuid, deck);
        return;
    }

    // Draw delivery: put the picked card on top and let the game draw it.
    if (deliverByDraw && state.pickedGuid >= 0) {
        Obj *system = AbilitySystemFor(model);
        auto *handEntity = FindEntity(model, hand);
        const int index = FindEntry(entries, state.pickedGuid, reserved);
        if (system && handEntity && index >= 0) {
            current = previous;
            Obj *entry = entries[index];
            if (MoveToTop(list, index, entry)) {
                {
                    std::lock_guard lock(stateMutex);
                    reservedEntries[{model, deck}].push_back(entry);
                }
                drawCardsFromDeck[system](handEntity, effect, 1);
                LOG_INFO("Tutor: [model %p] source guid %d tutored guid %d from deck %d (draw)",
                         model, sourceGuid, state.pickedGuid, deck);
                return;
            }
            LOG_ERR("Tutor: could not move guid %d to the top of deck %d", state.pickedGuid, deck);
            current = &state;
        } else if (verbose) {
            LOG_INFO("Tutor: draw delivery unavailable (system %p, hand %p, entry %d); conjuring from deck",
                     system, handEntity, index);
        }
    }

    // Conjure delivery (v2): the conjure creates the picked card, then one copy leaves the deck.
    originalProcessSubset(self, effect, method);
    current = previous;

    const int drawn = subsetDrawnId[effect]();
    const int drawnGuid = GuidOf(FindEntity(model, drawn));
    if (!state.pickCalled) {
        LOG_WARN("Tutor: source guid %d conjured guid %d without calling PickRandomGuid; deck unchanged", sourceGuid, drawnGuid);
    } else if (state.pickedGuid < 0) {
        if (verbose) LOG_INFO("Tutor: source guid %d found no deck match; conjured guid %d normally", sourceGuid, drawnGuid);
    } else if (drawnGuid != state.pickedGuid) {
        LOG_WARN("Tutor: picked guid %d but the hand got guid %d; deck unchanged", state.pickedGuid, drawnGuid);
    } else if (int index = FindEntry(Items(list), drawnGuid, reserved); index >= 0 && RemoveEntry(list, index)) {
        LOG_INFO("Tutor: [model %p] source guid %d tutored guid %d from deck %d (conjure)", model, sourceGuid, drawnGuid, deck);
    } else {
        LOG_WARN("Tutor: guid %d was not found in deck %d to remove", drawnGuid, deck);
    }
}

static bool Bind(BNM::FieldBase field, const char *what) {
    if (!field.IsValid()) LOG_ERR("Tutor: field not found: %s", what);
    return field.IsValid();
}

static bool Bind(BNM::MethodBase method, const char *what) {
    if (!method.IsValid()) LOG_ERR("Tutor: method not found: %s", what);
    return method.IsValid();
}

static void Init() {
    if (!BLOOM_CONFIG_BOOL("General", "Enabled", true)) return;
    verbose = BLOOM_CONFIG_BOOL("General", "Verbose", false);
    fizzle = BLOOM_CONFIG_BOOL("General", "Fizzle", true);
    deliverByDraw = BLOOM_CONFIG("General", "Delivery") != "conjure";

    if (void *il2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD)) {
        gcNewWeak = reinterpret_cast<decltype(gcNewWeak)>(dlsym(il2cpp, "il2cpp_gchandle_new_weakref"));
        gcTarget = reinterpret_cast<decltype(gcTarget)>(dlsym(il2cpp, "il2cpp_gchandle_get_target"));
        gcFree = reinterpret_cast<decltype(gcFree)>(dlsym(il2cpp, "il2cpp_gchandle_free"));
    }
    if (deliverByDraw && (!gcNewWeak || !gcTarget || !gcFree)) {
        LOG_WARN("Tutor: IL2CPP GC handle API not found; using conjure delivery");
        deliverByDraw = false;
    }

    const auto entity = BNM::Class("PvZCards.Engine", "Entity");
    const auto effect = BNM::Class("PvZCards.Engine", "Effect");
    const auto subset = BNM::Class("PvZCards.Core.Systems", "DrawCardFromSubsetSystem");
    const auto draw = BNM::Class("PvZCards.Core.Systems", "DrawCardSystem");
    const auto ability = BNM::Class("PvZCards.Core.Systems", "DrawCardAbilitySystem");
    const auto finder = BNM::Class("PvZCards.Core.Systems.Utilities", "CardFromSubsetFinder");
    const auto subsetEffect = BNM::Class("PvZCards.Engine.Effects", "DrawCardFromSubsetEffect");
    const auto drawEffect = BNM::Class("PvZCards.Engine.Effects", "DrawCardEffect");
    const auto entry = BNM::Class("PvZCards.Engine.Components.Data", "CardPoolEntry");

    bool ok = true;
    ok &= Bind(effectSource = effect.GetField("SourceId"), "Effect.SourceId");
    ok &= Bind(effectPlayer = effect.GetField("PlayerId"), "Effect.PlayerId");
    ok &= Bind(effectCancel = effect.GetMethod("Cancel", 0), "Effect.Cancel");
    ok &= Bind(modelEntities = BNM::Class("PvZCards.Engine", "EntityModel").GetField("Entities"), "EntityModel.Entities");
    ok &= Bind(entityComponents = entity.GetField("Components"), "Entity.Components");
    ok &= Bind(entityId = entity.GetField("<Id>k__BackingField"), "Entity.Id");
    ok &= Bind(subsetSystemModel = subset.GetField("<Model>k__BackingField"), "DrawCardFromSubsetSystem.Model");
    ok &= Bind(subsetSystemFinder = subset.GetField("<CardFromSubsetFinder>k__BackingField"), "DrawCardFromSubsetSystem.CardFromSubsetFinder");
    ok &= Bind(drawSystemModel = draw.GetField("<Entities>k__BackingField"), "DrawCardSystem.Entities");
    ok &= Bind(abilitySystemModel = ability.GetField("<EntityModel>k__BackingField"), "DrawCardAbilitySystem.EntityModel");
    ok &= Bind(drawCardsFromDeck = ability.GetMethod("DrawCardsFromDeck", 3), "DrawCardAbilitySystem.DrawCardsFromDeck");
    ok &= Bind(subsetEffectQuery = subsetEffect.GetField("SubsetQuery"), "DrawCardFromSubsetEffect.SubsetQuery");
    ok &= Bind(compositeQueries = BNM::Class("PvZCards.Engine.Queries", "CompositeQuery").GetField("Queries"), "CompositeQuery.Queries");
    ok &= Bind(notQueryInner = BNM::Class("PvZCards.Engine.Queries", "NotQuery").GetField("Query"), "NotQuery.Query");
    ok &= Bind(hasComponentType = BNM::Class("PvZCards.Engine.Queries", "HasComponentQuery").GetField("ComponentType"), "HasComponentQuery.ComponentType");
    ok &= Bind(subsetHandId = subsetEffect.GetMethod("get_HandId", 0), "DrawCardFromSubsetEffect.HandId");
    ok &= Bind(subsetDrawnId = subsetEffect.GetMethod("get_DrawnCardId", 0), "DrawCardFromSubsetEffect.DrawnCardId");
    ok &= Bind(drawHandId = drawEffect.GetMethod("get_HandId", 0), "DrawCardEffect.HandId");
    ok &= Bind(drawDeckId = drawEffect.GetMethod("get_DeckId", 0), "DrawCardEffect.DeckId");
    ok &= Bind(finderFindCard = finder.GetMethod("FindCard", {"subsetQuery", "effect"}), "CardFromSubsetFinder.FindCard(subsetQuery, effect)");
    ok &= Bind(cardGuid = BNM::Class("PvZCards.Engine.Components", "Card").GetField("Guid"), "Card.Guid");
    ok &= Bind(poolEntries = BNM::Class("PvZCards.Engine.Components", "OrderedCardPool").GetField("Entries"), "OrderedCardPool.Entries");
    ok &= Bind(entryGuid = entry.GetField("Guid"), "CardPoolEntry.Guid");
    ok &= Bind(entryEntityId = entry.GetField("EntityId"), "CardPoolEntry.EntityId");
    auto processSubset = subset.GetMethod("ProcessEffect", 1);
    auto processDraw = draw.GetMethod("ProcessEffect", 1);
    auto pickRandomGuid = finder.GetMethod("PickRandomGuid", 1);
    auto registerHandlers = ability.GetMethod("RegisterEffectHandlers", 1);
    ok &= Bind(processSubset, "DrawCardFromSubsetSystem.ProcessEffect");
    ok &= Bind(processDraw, "DrawCardSystem.ProcessEffect");
    ok &= Bind(pickRandomGuid, "CardFromSubsetFinder.PickRandomGuid");
    ok &= Bind(registerHandlers, "DrawCardAbilitySystem.RegisterEffectHandlers");
    if (!ok) return;

    BNM::BasicHook(registerHandlers, RegisterHandlers, originalRegisterHandlers);
    BNM::BasicHook(processDraw, ProcessDraw, originalProcessDraw);
    BNM::BasicHook(pickRandomGuid, PickRandomGuid, originalPickRandomGuid);
    BNM::BasicHook(processSubset, ProcessSubset, originalProcessSubset);
    if (!originalRegisterHandlers || !originalProcessDraw || !originalPickRandomGuid || !originalProcessSubset) {
        LOG_ERR("Tutor: hook failed (register %d, draw %d, pick %d, conjure %d)", originalRegisterHandlers != nullptr,
                originalProcessDraw != nullptr, originalPickRandomGuid != nullptr, originalProcessSubset != nullptr);
        return;
    }
    LOG_INFO("Tutor v5: ready; fizzle %s, delivery %s", fizzle ? "on" : "off", deliverByDraw ? "draw" : "conjure");
}

BLOOM_REGISTER_PNP(Init);
