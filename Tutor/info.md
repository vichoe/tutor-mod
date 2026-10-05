# Tutor
In other card games, Tutoring is "Draw a card from X type that is in your deck". With the assist of Claude (5% me 95% Claude) I made it real!!

How it works: Reads the player's deck and pre-checks with the game's finder whetever anything in the game matches without consuming RNG. If there's no match, the effect fizzles by default. If there is a match, the entry is moved to the top of the deck, and draw a card is called, while the action of conjuring is skipped.

How to implement: add the following query to drawCardFromSubset which acts as a marker for Tutoring

```
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

Example (Photosynthetizer)

```
{
	"entity" : {
		"components" : [
			{
				"$type" : "PvZCards.Engine.Components.Card, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {
					"Guid" : 417
				}
			},
			{
				"$type" : "PvZCards.Engine.Components.Burst, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {}
			},
			{
				"$type" : "PvZCards.Engine.Components.SunCost, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {
					"SunCostValue" : {
						"BaseValue" : 1
					}
				}
			},
			{
				"$type" : "PvZCards.Engine.Components.Plants, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {}
			},
			{
				"$type" : "PvZCards.Engine.Components.Rarity, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {
					"Value" : "R1"
				}
			},
			{
				"$type" : "PvZCards.Engine.Components.Tags, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {
					"tags" : [ "galaxyplant" ]
				}
			},
			{
				"$type" : "PvZCards.Engine.Components.EffectEntitiesDescriptor, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
				"$data" : {
					"entities" : [
						{
							"components" : [
								{
									"$type" : "PvZCards.Engine.Components.EffectEntityGrouping, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"AbilityGroupId" : 0
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.PlayTrigger, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {}
								},
								{
									"$type" : "PvZCards.Engine.Components.TriggerTargetFilter, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"Query" : {
											"$type" : "PvZCards.Engine.Queries.SelfQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
											"$data" : {}
										}
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.PrimaryTargetFilter, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"SelectionType" : "Manual",
										"NumTargets" : 0,
										"TargetScopeType" : "All",
										"TargetScopeSortValue" : "None",
										"TargetScopeSortMethod" : "None",
										"AdditionalTargetType" : "None",
										"AdditionalTargetQuery" : null,
										"OnlyApplyEffectsOnAdditionalTargets" : false,
										"Query" : {
											"$type" : "PvZCards.Engine.Queries.CompositeAllQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
											"$data" : {
												"queries" : [
													{
														"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"ComponentType" : "PvZCards.Engine.Components.Plants, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
														}
													},
													{
														"$type" : "PvZCards.Engine.Queries.TargetableInPlayFighterQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {}
													}
												]
											}
										}
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.BuffEffectDescriptor, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"AttackAmount" : 0,
										"HealthAmount" : 2,
										"BuffDuration" : "Permanent"
									}
								}
							]
						},
						{
							"components" : [
								{
									"$type" : "PvZCards.Engine.Components.EffectEntityGrouping, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"AbilityGroupId" : 0
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.PlayTrigger, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {}
								},
								{
									"$type" : "PvZCards.Engine.Components.TriggerTargetFilter, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"Query" : {
											"$type" : "PvZCards.Engine.Queries.SelfQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
											"$data" : {}
										}
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.PrimaryTargetFilter, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"SelectionType" : "All",
										"NumTargets" : 0,
										"TargetScopeType" : "All",
										"TargetScopeSortValue" : "None",
										"TargetScopeSortMethod" : "None",
										"AdditionalTargetType" : "None",
										"AdditionalTargetQuery" : null,
										"OnlyApplyEffectsOnAdditionalTargets" : false,
										"Query" : {
											"$type" : "PvZCards.Engine.Queries.CompositeAllQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
											"$data" : {
												"queries" : [
													{
														"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"ComponentType" : "PvZCards.Engine.Components.Plants, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
														}
													},
													{
														"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"ComponentType" : "PvZCards.Engine.Components.Player, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
														}
													}
												]
											}
										}
									}
								},
								{
									"$type" : "PvZCards.Engine.Components.DrawCardFromSubsetEffectDescriptor, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
									"$data" : {
										"SubsetQuery" : {
											"$type" : "PvZCards.Engine.Queries.CompositeAllQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
											"$data" : {
												"queries" : [
													{
														"$type" : "PvZCards.Engine.Queries.NotQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"Query" : {
																"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
																"$data" : {
																	"ComponentType" : "PvZCards.Engine.Components.Superpower, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
																}
															}
														}
													},
													{
														"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"ComponentType" : "PvZCards.Engine.Components.Plants, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
														}
													},
													{
														"$type" : "PvZCards.Engine.Queries.TrickQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {}
													},
													{
														"$type" : "PvZCards.Engine.Queries.NotQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
														"$data" : {
															"Query" : {
																"$type" : "PvZCards.Engine.Queries.HasComponentQuery, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null",
																"$data" : {
																	"ComponentType" : "PvZCards.Engine.Components.Deck, EngineLib, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"
																}
															}
														}
													}
												]
											}
										},
										"DrawAmount" : 1
									}
								}
							]
						}
					]
				}
			}
		]
	},
	"prefabName" : "Gravitational Pull",
	"baseId" : "BasePlantOneTimeEffect",
	"color" : "Guardian",
	"set" : "Set2",
	"rarity" : 0,
	"setAndRarityKey" : "Galaxy_Common",
	"craftingBuy" : 50,
	"craftingSell" : 15,
	"displayHealth" : 0,
	"displayAttack" : 0,
	"displaySunCost" : 1,
	"faction" : "Plants",
	"ignoreDeckLimit" : false,
	"isPower" : false,
	"isPrimaryPower" : false,
	"isFighter" : false,
	"isEnv" : false,
	"isAquatic" : false,
	"isTeamup" : false,
	"subtypes" : [],
	"tags" : [ "galaxyplant" ],
	"subtype_affinities" : [],
	"subtype_affinity_weights" : [],
	"tag_affinities" : [],
	"tag_affinity_weights" : [],
	"card_affinities" : [ 449 ],
	"card_affinity_weights" : [ 1.3 ],
	"usable" : true,
	"special_abilities" : []
}
```

# Config

Fizzle: If there's no match in the deck, `true` does nothing and `false` conjures normally.

Delivery: `draw` draws a card as usual, `conjure` creates the card and removes a copy of that card from your deck (it counts as conjured)

Verbose: Logs decisions, including the AI's decisions (mainly for debugging)