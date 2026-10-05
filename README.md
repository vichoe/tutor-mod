# Tutor

A module for [Sprig](https://github.com/10buttonmushrooms/Sprig) by 10buttonmushrooms, the native modding base for
Plants vs. Zombies Heroes on Android. It adds a **"draw a card of type X from your deck"**
mechanic (a *tutor*) to the game.

It needs no new effect type: any existing **conjure** ability becomes a tutor when its query
carries a marker. Which cards tutor is decided entirely by card data.

- The game's own query decides which cards qualify, so any conjure filter works: tribe, class,
  cost, Trick, set, and so on.
- Copies are weighted: four copies in the deck are four times as likely as one.
- If nothing in the deck matches, the ability **fizzles** (does nothing).
- Conjures without the marker are passed to the game untouched.

## Requirements

- A working [Sprig](https://github.com/10buttonmushrooms/Sprig) setup. Follow its README until
  the stock build installs and runs on your device. This repository contains only the module;
  it is not a copy of Sprig.
- A way to change card data, since the module only adds the mechanic. For example, a replaced
  card data bundle (edited with UABE or a similar tool), or any mod that patches card JSON at
  load time.

## Install

1. Copy the `Tutor` folder into your Sprig `Bloom/` folder, so you have `Bloom/Tutor/main.cpp`.
2. Build and install as usual with Sprig (`.\work\scripts\all.ps1`).
3. Check `adb logcat -s Sprig` for:
   ```
   Tutor v5: ready; fizzle on, delivery draw
   ```

## Making an ability draw from the deck

Take a conjure ability (`DrawCardFromSubsetEffectDescriptor`) and add this **marker** to its
`SubsetQuery`:

```json
{
  "$type": "PvZCards.Engine.Queries.NotQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
  "$data": {
    "Query": {
      "$type": "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
      "$data": {
        "ComponentType": "PvZCards.Engine.Components.Deck, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
      }
    }
  }
}
```

"Is not a Deck" is true for every card, so the marker never changes what matches; it only labels
the ability for this module. Without the module installed, a marked card simply conjures as
before.

The marker is the **only** thing that makes an ability a tutor. There is no card list, tag or
setting that does it.

Rules:

1. The marker must be a **direct item of a `CompositeAllQuery`'s `queries` list**. Inside a
   `CompositeAnyQuery` or a `NotQuery` it is ignored on purpose, because there it would change
   the result.
2. If the `SubsetQuery` is a single query, wrap it: a `CompositeAllQuery` whose `queries` are the
   original query plus the marker.
3. Everything else is normal card data: `DrawAmount` sets how many cards, and the ability's
   trigger (`PlayTrigger`, `TurnStartTrigger`, ...) sets when.
4. Update the card's text yourself (for example "Conjure" to "Draw"). The module does not change
   any text.

## Examples

The abilities below are shortened: `$type` prefixes are removed and only the effect is shown.
The trigger and target parts of each ability are ordinary card data.

### Primal Wall-Nut (card 646): the smallest possible change

The original card reads "When played: Conjure a card that costs 4 or more". Adding the marker
is the only edit needed to make it "Draw a card that costs 4 or more from your deck":

```
DrawCardFromSubsetEffectDescriptor
  DrawAmount: 1
  SubsetQuery: CompositeAllQuery
    queries:
      NotQuery(HasComponentQuery Superpower)   not a Superpower
      SunCostComparisonQuery >= 4              costs 4 or more
      HasComponentQuery Plants                 a Plant card
      NotQuery(HasComponentQuery Deck)         marker: take it from the deck
```

### Gardening Gloves (card 298): "Move a Plant. Draw a Trick."

The second ability of the card:

```
DrawCardFromSubsetEffectDescriptor
  DrawAmount: 1
  SubsetQuery: CompositeAllQuery
    queries:
      NotQuery(HasComponentQuery Superpower)   not a Superpower
      HasComponentQuery Plants                 a Plant card
      TrickQuery                               a Trick
      NotQuery(HasComponentQuery Deck)         marker: take it from the deck
```

Removing the last line turns it back into "Conjure a Plant Trick".

### Bamboozle (card 602): two tutors on one trigger

A reworked Bamboozle: "Plant Evolution: Draw a Plant and a Trick". The original card has one
Evolution ability that draws two cards. Here that ability is duplicated, and each copy gets its
own marked conjure in place of the plain draw:

```
Ability 1 (on Evolution)
DrawCardFromSubsetEffectDescriptor
  DrawAmount: 1
  SubsetQuery: CompositeAllQuery
    queries:
      NotQuery(HasComponentQuery Superpower)   not a Superpower
      HasComponentQuery Plants                 a Plant card
      FighterQuery                             a fighter (not a Trick or Environment)
      NotQuery(HasComponentQuery Deck)         marker

Ability 2 (on Evolution)
DrawCardFromSubsetEffectDescriptor
  DrawAmount: 1
  SubsetQuery: CompositeAllQuery
    queries:
      NotQuery(HasComponentQuery Superpower)   not a Superpower
      HasComponentQuery Plants                 a Plant card
      TrickQuery                               a Trick
      NotQuery(HasComponentQuery Deck)         marker
```

Each ability is handled separately, so a deck with no Tricks left still gives you the Plant.

## Configuration (`config.ini`)

The config never decides *which* cards tutor (that is card data only). It controls how a tutor
behaves:

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | `true` | Turns the module off without removing it. |
| `Fizzle` | `true` | No match in the deck: `true` does nothing, `false` conjures normally (ala HU Pluck). |
| `Delivery` | `draw` | `draw`: a real draw. `conjure`: create the card, then remove one copy from the deck (counts as conjured and drawn). |
| `Verbose` | `false` | Logs every decision, including the AI's simulated plays. |

Changing the config needs a rebuild, like any Sprig module.

## How it works

| Hook | Purpose |
|---|---|
| `DrawCardFromSubsetSystem.ProcessEffect` | Every conjure. Detects the marker and runs the tutor. |
| `CardFromSubsetFinder.PickRandomGuid` | Restricts the game's candidate list to cards that are in the deck. |
| `DrawCardSystem.ProcessEffect` | Learns which deck feeds which hand from the opening draws. |
| `DrawCardAbilitySystem.RegisterEffectHandlers` | Remembers the draw system of each game (weak GC handles). |

For a marked conjure it:

1. reads the player's deck (`OrderedCardPool.Entries`; index 0 is the top);
2. **pre-checks** with the game's finder (`CardFromSubsetFinder.FindCard`) whether anything in
   the deck matches, without consuming the game's random numbers;
3. no match: cancels the effect (fizzle);
4. match: moves that entry to the top of the deck and calls
   `DrawCardAbilitySystem.DrawCardsFromDeck(hand, effect, 1)`, the code behind every
   "Draw a card". The original conjure is skipped.

## Limitations

- **"...and it costs 1 less" style follow-ups are skipped.** Effects that the game attaches to a
  conjured card (`HeraldEntities`, as on Cheese Cutter or Cosmic Pea) belong to the conjure that
  this module replaces, so with `Delivery=draw` they are not applied to the drawn card.
- **Cards tagged `cheats` cannot be found.** The game's finder leaves them out of every random
  pick, and the module asks that finder for its candidates. This affects a few tokens, such as
  Magic Beanstalk.
- **Offline, single-player only.** An unmodded opponent or server would not agree on the result.

## Compatibility

Two mods cannot hook the same function. Any other module that hooks
`DrawCardFromSubsetSystem.ProcessEffect`, `CardFromSubsetFinder.PickRandomGuid`,
`DrawCardSystem.ProcessEffect` or `DrawCardAbilitySystem.RegisterEffectHandlers` will conflict
with this one.

## Logs

`adb logcat -s Sprig` (set `Verbose=true` for more):

```
Tutor: [model 0x...] source guid 298 tutored guid 41 from deck 1 (draw)
Tutor: [model 0x...] source guid 298 found nothing in deck 1; fizzled
```

A `field not found` or `method not found` line at startup means the game version differs from
the one this was written for. The module then disables itself and the game runs unmodified.

## Credits

- [Sprig](https://github.com/10buttonmushrooms/Sprig) by 10buttonmushrooms: the modding base
  this module is built for, including the Bloom module system it plugs into.

Plants vs. Zombies Heroes is a trademark of Electronic Arts. This is an unofficial fan project
and is not affiliated with or endorsed by EA or PopCap.
