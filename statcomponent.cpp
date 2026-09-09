#include "StatComponent.h"
#include "StatSet.h"
#include "Engine/World.h"
#include "PlayerState.h"
#include "InventoryComponent.h"
#include "TimerManager.h"

UStatComponent::UStatComponent(): CachedActor(nullptr), CachedPlayerState(nullptr),
                                              StatSet(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Default Stats --- Gets overridden by StatSet values if valid! --- 
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Health")), FStat(100.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Poise")), FStat(100.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Energy")), FStat(100.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Stamina")), FStat(100.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Oxygen")), FStat(100.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Damage")), FStat(20.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Stat.Armor")), FStat(10.f));

	// Default Resistances
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Fire")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Frost")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Lightning")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Poison")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Shadow")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Slash")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Blunt")), FStat(0.f));
	Stats.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Piercing")), FStat(0.f));
}

void UStatComponent::BeginPlay()
{
	Super::BeginPlay();

	if (StatSet)
	{
		for (const FTaggedStat& Entry : StatSet->Stats)
		{
			FStat NewAttr;
			NewAttr.BaseValue = Entry.BaseValue;
			NewAttr.MaxValue = Entry.MaxValue;   // <-- uses your data asset value
			NewAttr.PermanentMax = Entry.MaxValue;
			NewAttr.CurrentValue = Entry.BaseValue; // optional — can set to Base or Max

			Stats.Add(Entry.Tag, NewAttr);
		}
	}

	// Initialize Stats (permanent max, modifiers, clamp)
	UpdateStats(false);

	// Cache owner refs
	RefreshCachedRefs();

	// Grant all startup actions
	for (TSubclassOf<URFAction> ActionClass : StartupActions)
	{
		GrantAction(ActionClass);
	}
}


// -- Actions Section ---

void UStatComponent::RebuildResistanceCache()
{
	DamageTagToResistanceCache.Empty();

	for (const auto& Pair : Stats)
	{
		const FGameplayTag& AttrTag = Pair.Key;

		// Only Stats starting with "Resistance"
		if (!AttrTag.ToString().StartsWith(TEXT("Resistance")))
			continue;

		// Compute corresponding Damage tag (e.g., Resistance.Fire -> Damage.Fire)
		FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(
			*AttrTag.ToString().Replace(TEXT("Resistance"), TEXT("Damage"))
		);

		// Store runtime resistance value (permanent + modifiers)
		DamageTagToResistanceCache.Add(DamageTag, Pair.Value.MaxValue);
	}
}


void UStatComponent::ApplyStatModifier(const FStatModifier& Modifier)
{
	if (FStat* Attr = Stats.Find(Modifier.StatTag))
	{
		float OldCurrent = Attr->CurrentValue;
		float OldMax     = Attr->MaxValue;

		// Current / Both
		if (Modifier.Target == EStatTarget::CurrentValue || Modifier.Target == EStatTarget::Both)
		{
			if (Modifier.ModifierType == EModifierType::Additive)
			{
				Attr->CurrentValue += Modifier.Magnitude;
			}
			else // Percent-based, relative to BaseValue
			{
				Attr->CurrentValue += Attr->BaseValue * Modifier.Magnitude;
			}
		}

		// Max / Both
		if (Modifier.Target == EStatTarget::MaxValue || Modifier.Target == EStatTarget::Both)
		{
			if (Modifier.ModifierType == EModifierType::Additive)
			{
				Attr->MaxValue += Modifier.Magnitude;
			}
			else // Percent-based, relative to BaseValue
			{
				Attr->MaxValue += Attr->BaseValue * Modifier.Magnitude;
			}
		}

		// Clamp current to [0, Max]
		Attr->CurrentValue = FMath::Clamp(Attr->CurrentValue, 0.f, Attr->MaxValue);

		// Broadcast changes
		if (!FMath::IsNearlyEqual(OldCurrent, Attr->CurrentValue))
		{
			OnStatChanged.Broadcast(Modifier.StatTag, Attr->CurrentValue);
		}
		if (!FMath::IsNearlyEqual(OldMax, Attr->MaxValue))
		{
			OnStatChanged.Broadcast(Modifier.StatTag, Attr->MaxValue);
		}
		// --- Rebuild resistance cache if this Stat is a resistance ---
		if (Modifier.StatTag.GetTagName().ToString().Contains(TEXT("Resistance.")))
		{
			RebuildResistanceCache();
		}
	}
}


//Tracking Item StatModifiers
void UStatComponent::AddItemModifierTag(const FGameplayTag& Tag)
{
	int32& Count = ItemModifiedStats.FindOrAdd(Tag);
	Count++;
}

void UStatComponent::RemoveItemModifierTag(const FGameplayTag& Tag)
{
	if (int32* Count = ItemModifiedStats.Find(Tag))
	{
		(*Count)--;

		if (*Count <= 0)
		{
			ItemModifiedStats.Remove(Tag);
		}
	}
}
//////////////////////////

FStat* UStatComponent::GetStatRef(FGameplayTag StatTag)
{
	return Stats.Find(StatTag); // returns FStat*, might be nullptr
}

float UStatComponent::GetStatValue(FGameplayTag StatTag, EStatTarget Target) const
{
	if (const FStat* Attr = Stats.Find(StatTag))
	{
		switch (Target)
		{
		case EStatTarget::CurrentValue: return Attr->CurrentValue;
		case EStatTarget::MaxValue:     return Attr->MaxValue;
		case EStatTarget::Both:         return Attr->CurrentValue; // default to current
		}
	}
	return 0.f;
}




// --- UpdateStats and apply levelpoints --- 
void UStatComponent::UpdateStats(bool bClampToMax /*= true*/)
{
	const FGameplayTag XPTag = FGameplayTag::RequestGameplayTag(FName("Stat.XP"));
	const FGameplayTag PointsTag = FGameplayTag::RequestGameplayTag(FName("Stat.SpendablePoints"));

	for (auto& Pair : Stats)
	{
		FGameplayTag Tag = Pair.Key;
		FStat& Attr = Pair.Value;

		// --- Skip XP and SpendablePoints since they handle their own progression ---
		if (Tag.MatchesTagExact(XPTag) || Tag.MatchesTagExact(PointsTag))
		{
			continue;
		}

		float OldCurrent   = Attr.CurrentValue;
		float OldMax       = Attr.MaxValue;
		float OldPermanent = Attr.PermanentMax;

		// --- Recalculate permanent max (points scaling) ---
		if (Attr.BaseValue > 0.f)
		{
			Attr.PermanentMax = Attr.BaseValue * FMath::Sqrt(static_cast<float>(Attr.PointsSpent + 1));
			Attr.MaxValue = Attr.PermanentMax;
		}

		// --- Clamp CurrentValue ---
		if (bClampToMax)
		{
			Attr.CurrentValue = Attr.MaxValue;
		}
		else
		{
			Attr.CurrentValue = FMath::Clamp(Attr.CurrentValue, 0.f, Attr.MaxValue);
		}

		// --- Broadcast changes ---
		if (!FMath::IsNearlyEqual(OldCurrent, Attr.CurrentValue))
		{
			OnStatChanged.Broadcast(Tag, Attr.CurrentValue);
		}

		if (!FMath::IsNearlyEqual(OldMax, Attr.MaxValue) ||
			!FMath::IsNearlyEqual(OldPermanent, Attr.PermanentMax))
		{
			OnStatChanged.Broadcast(Tag, Attr.MaxValue);
		}

		// --- Rebuild resistance cache if this is a resistance Stat ---
		if (Tag.ToString().Contains(TEXT("Resistance.")))
		{
			RebuildResistanceCache();
		}
	}
}


void UStatComponent::ClearItemModifiers()
{
	if (ItemModifiedStats.Num() == 0)
	{
		return;
	}

	// Iterate through all tags currently modified by one or more items
	for (auto It = ItemModifiedStats.CreateIterator(); It; ++It)
	{
		const FGameplayTag& Tag = It.Key();
		int32 Count = It.Value();

		if (Count <= 0)
		{
			continue; // Skip any invalid entries (shouldn't normally happen)
		}

		if (FStat* Attr = Stats.Find(Tag))
		{
			// Reset Stat to its permanent (non-item) value
			Attr->MaxValue = Attr->PermanentMax;
			Attr->CurrentValue = FMath::Clamp(Attr->CurrentValue, 0.f, Attr->MaxValue);

			// Notify listeners/UI
			OnStatChanged.Broadcast(Tag, Attr->CurrentValue);
		}
	}

	// ✅ Clear all entries, since we’ll reapply current equipment afterward
	ItemModifiedStats.Empty();
}

void UStatComponent::GrantExperience(float Amount)
{
    if (Amount <= 0.f)
        return;

    static const FGameplayTag XPTag = FGameplayTag::RequestGameplayTag(FName("Stat.XP"));
    static const FGameplayTag SpendablePointsTag = FGameplayTag::RequestGameplayTag(FName("Stat.SpendablePoints"));

    // Ensure LevelUpGrowthFactor has a sane default (e.g. 0.1 = +10%)
    if (LevelUpGrowthFactor <= 0.f)
        LevelUpGrowthFactor = 0.1f;

    // Find or create XP Stat
    FStat* XPAttr = Stats.Find(XPTag);
    if (!XPAttr)
    {
        FStat NewXP;
        NewXP.CurrentValue = 0.f;
        NewXP.MaxValue = 100.f;
        NewXP.BaseValue = 0.f;
        NewXP.PermanentMax = 100.f;
        Stats.Add(XPTag, NewXP);
        XPAttr = Stats.Find(XPTag);
    }

    FStat& XP = *XPAttr;

    XP.CurrentValue += Amount;
    OnStatChanged.Broadcast(XPTag, XP.CurrentValue);

    // --- Level Up Check ---
    bool bLeveledUp = false;

    // Avoid infinite loops by limiting iterations (in case of bad data)
    int32 SafetyCounter = 0;
    const int32 MaxIterations = 100;

    while (XP.CurrentValue >= XP.MaxValue && SafetyCounter++ < MaxIterations)
    {
        bLeveledUp = true;

        // Prevent zero or negative MaxValue
        XP.MaxValue = FMath::Max(XP.MaxValue, 1.f);

        XP.CurrentValue -= XP.MaxValue;
        XP.MaxValue *= (1.f + LevelUpGrowthFactor);
        XP.PermanentMax = XP.MaxValue;
        CurrentLevel++;

        // Log for debugging
        UE_LOG(LogTemp, Warning, TEXT("Level Up! New Level: %d, New Max XP: %.2f"), CurrentLevel, XP.MaxValue);

        // ✅ Grant Spendable Points
        FStatModifier RewardModifier;
        RewardModifier.StatTag = SpendablePointsTag;
        RewardModifier.Target = EStatTarget::CurrentValue;
        RewardModifier.ModifierType = EModifierType::Additive;
        RewardModifier.Magnitude = SpendablePointsPerLevel;
        ApplyStatModifier(RewardModifier);

        // Fire Blueprint-native event
        OnLevelUp.Broadcast(CurrentLevel);
    }

    if (SafetyCounter >= MaxIterations)
    {
        UE_LOG(LogTemp, Error, TEXT("GrantExperience hit safety limit — possible XP config error (XP.MaxValue=%.2f, XP.Current=%.2f)"), XP.MaxValue, XP.CurrentValue);
    }

    if (bLeveledUp)
    {
        OnStatChanged.Broadcast(XPTag, XP.CurrentValue);
    }
}



// --- Fix so Default is Damage.Physical ---
float UStatComponent::GetResistanceForDamage(const FGameplayTag& DamageTag)
{
	// Try to find the resistance in the cache
	for (const auto& Pair : DamageTagToResistanceCache)
	{
		if (DamageTag.MatchesTag(Pair.Key))
			return Pair.Value;
	}

	return 0.f;
}


float UStatComponent::CalculateDamageReduction(float Value, float CapAtValue, float MaxReduction) const
{
	if (Value <= 0.f || CapAtValue <= 0.f || MaxReduction <= 0.f)
	{
		return 0.f;
	}

	// Classic diminishing returns curve
	float Reduction = Value / (Value + CapAtValue);

	// Scale to your desired max reduction (e.g., 0.8 = 80%)
	Reduction *= MaxReduction;

	return FMath::Clamp(Reduction, 0.f, MaxReduction);
}

void UStatComponent::ApplyDamage(const TArray<FDamageInfo>& DamageList)
{
    if (DamageList.Num() == 0)
        return;

    float TotalDamage = 0.f;
    float Armor = GetStatValue(RFStatTags::Armor);
	float Dodge = GetStatValue(RFStatTags::Dodge);
	float Parry = GetStatValue(RFStatTags::Parry);
	float Block = GetStatValue(RFStatTags::Block);

    // --- Instigator stats (one instigator per DamageList) ---
    UStatComponent* InstigatorStatComp = DamageList[0].InstigatorStatComp;
    FInstigatorStats Stats;

    if (InstigatorStatComp)
    {
        Stats.BaseDamage       = InstigatorStatComp->GetStatValue(RFStatTags::Damage);
        Stats.CritChance       = InstigatorStatComp->GetStatValue(RFStatTags::CritChance);
        Stats.CritMultiplier   = InstigatorStatComp->GetStatValue(RFStatTags::CritDamage);
        Stats.LifeStealPercent = InstigatorStatComp->GetStatValue(RFStatTags::LifeSteal);
    }
    else
    {
        Stats.BaseDamage       = 1.f;
        Stats.CritChance       = 0.f;
        Stats.CritMultiplier   = 1.f;
        Stats.LifeStealPercent = 0.f;
    }

// --- Roll avoidance/block once for the whole hit ---
const float Roll = FMath::FRand();

if (Roll < Dodge)
{
    OnDamageResolved.Broadcast(EDamageOutcome::Dodged);
    return;
}

if (Roll < Dodge + Parry)
{
    OnDamageResolved.Broadcast(EDamageOutcome::Parried);
    return;
}

float BlockDamageReduction = 0.f;
EDamageOutcome DamageOutcome = EDamageOutcome::Hit;

if (Roll < Dodge + Parry + Block)
{
    BlockDamageReduction = 0.5f;
    DamageOutcome = EDamageOutcome::Blocked;
}


// --- Roll crit once for the whole hit ---
bool bCrit = false;

if (Stats.CritChance > 0.f &&
    FMath::FRand() <= Stats.CritChance)
{
    bCrit = true;

    // Only replace the outcome if it wasn't blocked.
    if (DamageOutcome == EDamageOutcome::Hit)
    {
        DamageOutcome = EDamageOutcome::CriticalHit;
    }
}


// --- Loop over damage parts ---
for (const FDamageInfo& Info : DamageList)
{
    const bool bIsDoT =
        Info.DamageType.GetTagName().ToString().Contains("DoT");

    float IncomingDamage =
        bIsDoT
        ? Info.Magnitude
        : Info.Magnitude * Stats.BaseDamage;

    float ArmorReduction = 0.f;

    if (Info.DamageType.MatchesTag(
        FGameplayTag::RequestGameplayTag(
            FName("Damage.Physical"))))
    {
        ArmorReduction = CalculateDamageReduction(
            Armor,
            ArmorCapValue,
            DamageReductionCap);
    }

    const float AfterBlock =
        IncomingDamage * (1.f - BlockDamageReduction);

    const float AfterArmor =
        AfterBlock * (1.f - ArmorReduction);

    float ResistanceValue =
        GetResistanceForDamage(Info.DamageType);

    float ResistReduction =
        CalculateDamageReduction(
            ResistanceValue,
            ResistCapValue,
            DamageReductionCap);

    const float AfterResist =
        AfterArmor * (1.f - ResistReduction);

    float FinalDamage =
        bCrit
        ? AfterResist * Stats.CritMultiplier
        : AfterResist;

    FinalDamage = FMath::Max(FinalDamage, 0.f);

    TotalDamage += FinalDamage;
    

        // Debug
        if (bDebugDamage)
        {
            UE_LOG(LogTemp, Warning, TEXT(
                "DamageInfo: %.2f | Incoming: %.2f | Armor: %.2f | ArmorRed: %.2f | AfterArmor: %.2f | "
                "Resist: %.2f | ResistRed: %.2f | AfterResist: %.2f | Crit: %s | LifeSteal: %.2f | FinalDamage: %.2f | TotalDamage: %.2f"),
                Info.Magnitude,
                IncomingDamage,
                Armor,
                ArmorReduction,
                AfterArmor,
                ResistanceValue,
                ResistReduction,
                AfterResist,
                bCrit ? TEXT("YES") : TEXT("NO"),
                bIsDoT ? 0.f : Stats.LifeStealPercent,
                FinalDamage,
                TotalDamage,
            );
        }

        // Lifesteal (only for non-DoTs)
        if (!bIsDoT && Stats.LifeStealPercent > 0.f && InstigatorStatComp)
        {
            FStat* InstigatorHealthPtr = InstigatorStatComp->GetStatRef(RFStatTags::Health);
            if (InstigatorHealthPtr)
            {
                FStat& InstigatorHealth = *InstigatorHealthPtr;
                float Heal = FinalDamage * Stats.LifeStealPercent;
                InstigatorHealth.CurrentValue = FMath::Clamp(
                    InstigatorHealth.CurrentValue + Heal, 0.f, InstigatorHealth.MaxValue);
                InstigatorStatComp->OnStatChanged.Broadcast(RFStatTags::Health, InstigatorHealth.CurrentValue);
            }
        }
    }

    // --- Apply total damage to target ---
    FStat* HealthPtr = GetStatRef(RFStatTags::Health);
    if (HealthPtr)
    {
        FStat& Health = *HealthPtr;
        Health.CurrentValue = FMath::Clamp(Health.CurrentValue - TotalDamage, 0.f, Health.MaxValue);
        OnStatChanged.Broadcast(RFStatTags::Health, Health.CurrentValue);

        if (Health.CurrentValue <= 0.f && !bIsDead)
        {
            bIsDead = true;
            OnDeath.Broadcast(InstigatorStatComp);
        }
    }
//
    // --- Fire delegate once per hit ---
    OnDamageTaken.Broadcast(TotalDamage, DamageOutcome);
}


void UStatComponent::ApplyPeriodicEffect(
    FGameplayTag StatTag,
    float Magnitude,
    float Duration,
    float TickInterval,
    EModifierType ModifierType,
    UStatComponent* InstigatorStatComp,
    bool bIsDOTorHOT,
    FName EffectID,
    FGameplayTag DamageTypeTag,
    FGameplayTag WhileActiveTag
)
{
    // If no EffectID was provided, generate a unique one
    if (EffectID == NAME_None)
    {
        EffectID = FName(*FGuid::NewGuid().ToString());
    }

    // Stack if effect with same ID exists
    for (FTimedEffect& Effect : ActiveTimedEffects)
    {
        if (Effect.EffectID == EffectID)
        {
            Effect.StackCount++;
            Effect.TimeRemaining = Duration;
            Effect.TimeUntilNextTick = TickInterval > 0.f ? TickInterval : 0.f;

            Effect.Magnitude      = Magnitude;
            Effect.Duration       = Duration;
            Effect.TickInterval   = TickInterval;
            Effect.ModifierType   = ModifierType;
            Effect.DamageTypeTag  = DamageTypeTag;
            Effect.bIsDOTorHOT    = bIsDOTorHOT;
            Effect.InstigatorStatComp   = InstigatorStatComp;

            OnTimedEffectStackChanged.Broadcast(Effect.EffectID, Effect.StackCount, Effect.TimeRemaining);
            return;
        }
    }

    // Create new effect
    FTimedEffect NewEffect;
    NewEffect.EffectID        = EffectID;
    NewEffect.StatTag    = StatTag;
    NewEffect.DamageTypeTag   = DamageTypeTag;
    NewEffect.Magnitude       = Magnitude;
    NewEffect.ModifierType    = ModifierType;
    NewEffect.Duration        = Duration;
    NewEffect.TickInterval    = TickInterval;
    NewEffect.TimeRemaining   = Duration;
    NewEffect.TimeUntilNextTick = TickInterval > 0.f ? TickInterval : 0.f;
    NewEffect.bIsDOTorHOT     = bIsDOTorHOT;
    NewEffect.InstigatorStatComp   = InstigatorStatComp;
    NewEffect.StackCount      = 1;
	NewEffect.WhileActiveTag	= WhileActiveTag;

    ActiveTimedEffects.Add(NewEffect);

    // Immediate application
    if (TickInterval <= 0.f)
    {
        if (bIsDOTorHOT)
        {
        	// DOT/HOT goes through ApplyDamage
        	FDamageInfo DamageInfo;
        	DamageInfo.Magnitude      = Magnitude;
        	DamageInfo.DamageType     = DamageTypeTag;
        	DamageInfo.InstigatorStatComp = InstigatorStatComp;

        	TArray<FDamageInfo> DamageArray;
        	DamageArray.Add(DamageInfo);

        	ApplyDamage(DamageArray);
        }
        else
        {
            // Buff/debuff applied instantly
            FStatModifier Mod;
            Mod.StatTag   = StatTag;
            Mod.Magnitude      = Magnitude;
            Mod.ModifierType   = ModifierType;
            Mod.Target         = EStatTarget::Both;
            ApplyStatModifier(Mod);
        }
    }

    // Broadcast new effect
    OnTimedEffectAdded.Broadcast(NewEffect);
	if (WhileActiveTag.IsValid())
	{
		AddTag(WhileActiveTag);
	}
}

void UStatComponent::RemovePeriodicEffectsByTag(FGameplayTag StatTag)
{
	for (int32 i = ActiveTimedEffects.Num() - 1; i >= 0; --i)
	{
		FTimedEffect& Effect = ActiveTimedEffects[i];

		if (Effect.StatTag == StatTag)
		{
			if (!Effect.bIsDOTorHOT)
			{
				FStatModifier InverseMod;
				InverseMod.StatTag = Effect.StatTag;
				InverseMod.Target = EStatTarget::Both;
				InverseMod.ModifierType = Effect.ModifierType;

				if (Effect.ModifierType == EModifierType::Additive)
				{
					InverseMod.Magnitude = -Effect.Magnitude * Effect.StackCount;
				}
				else // Multiplicative
				{
					float TotalFactor = FMath::Pow(1.f + Effect.Magnitude, Effect.StackCount);
					InverseMod.Magnitude = (1.f / TotalFactor) - 1.f;
				}

				ApplyStatModifier(InverseMod);
			}

			// Broadcast UI event
			if (Effect.WhileActiveTag.IsValid())
			{
				RemoveTag(Effect.WhileActiveTag);
			}
			OnTimedEffectRemoved.Broadcast(Effect);
			ActiveTimedEffects.RemoveAt(i);
		}
	}
}


// -- GameplayTag Management ---

void UStatComponent::AddTag(FGameplayTag Tag)
{
	if (!OwnedTags.HasTag(Tag))
	{
		OwnedTags.AddTag(Tag);

		// Broadcast to listeners
		OnTagChanged.Broadcast(Tag, true);
		OnTagChangedBP.Broadcast();     // For Blueprints

		UE_LOG(LogTemp, Log, TEXT("UStatComponent::AddTag - Added Tag: %s"), *Tag.ToString());
	}
}

void UStatComponent::RemoveTag(FGameplayTag Tag)
{
	if (OwnedTags.HasTag(Tag))
	{
		OwnedTags.RemoveTag(Tag);

		// Broadcast to listeners
		OnTagChanged.Broadcast(Tag, false);
		OnTagChangedBP.Broadcast();     // For Blueprints

		UE_LOG(LogTemp, Log, TEXT("UStatComponent::RemoveTag - Removed Tag: %s"), *Tag.ToString());
	}
}

bool UStatComponent::HasTag(FGameplayTag Tag) const
{
	return OwnedTags.HasTag(Tag);
}


void UStatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    for (int32 i = ActiveTimedEffects.Num() - 1; i >= 0; --i)
    {
        FTimedEffect& Effect = ActiveTimedEffects[i];
        Effect.TimeRemaining -= DeltaTime;
        Effect.TimeUntilNextTick -= DeltaTime;

        if (Effect.bIsDOTorHOT)
        {
            // Apply DOT/HOT per tick
            if (Effect.TickInterval > 0.f && Effect.TimeUntilNextTick <= 0.f)
            {
            	// Apply DOT/HOT using ApplyDamage so resistances are counted
            	FDamageInfo DamageInfo;
            	DamageInfo.Magnitude      = Effect.Magnitude * Effect.StackCount;
            	DamageInfo.DamageType  = Effect.DamageTypeTag;
            	DamageInfo.InstigatorStatComp = Effect.InstigatorStatComp;

            	TArray<FDamageInfo> DamageArray;
            	DamageArray.Add(DamageInfo);
            	
            	ApplyDamage(DamageArray);

            	Effect.TimeUntilNextTick += Effect.TickInterval;
            }

            // Expire ALL stacks when timer runs out
            if (Effect.TimeRemaining <= 0.f)
            {
            	// --- Remove active tag ---
            	if (Effect.WhileActiveTag.IsValid())
            	{
            		RemoveTag(Effect.WhileActiveTag);
            	}
                OnTimedEffectRemoved.Broadcast(Effect);
                ActiveTimedEffects.RemoveAt(i);
            }
        }
        else
        {
            // Apply tick-based modifiers (rare for buffs, but supported)
            if (Effect.TickInterval > 0.f && Effect.TimeUntilNextTick <= 0.f)
            {
                float EffectiveMagnitude = Effect.Magnitude * Effect.StackCount;

                FStatModifier Modifier;
                Modifier.StatTag = Effect.StatTag;
                Modifier.Magnitude = EffectiveMagnitude;
                Modifier.ModifierType = Effect.ModifierType;
                Modifier.Target = EStatTarget::Both;

                ApplyStatModifier(Modifier);

                Effect.TimeUntilNextTick += Effect.TickInterval;
            }

            // Expire ALL stacks when timer runs out
            if (Effect.TimeRemaining <= 0.f)
            {
                // Revert total stacked effect
                FStatModifier InverseMod;
                InverseMod.StatTag = Effect.StatTag;
                InverseMod.ModifierType = Effect.ModifierType;
                InverseMod.Target = EStatTarget::Both;

                if (Effect.ModifierType == EModifierType::Additive)
                {
                    InverseMod.Magnitude = -Effect.Magnitude * Effect.StackCount;
                }
                else // Multiplicative
                {
                    float TotalFactor = FMath::Pow(1.f + Effect.Magnitude, Effect.StackCount);
                    InverseMod.Magnitude = (1.f / TotalFactor) - 1.f;
                }

                ApplyStatModifier(InverseMod);
            	// --- Remove active tag ---
            	if (Effect.WhileActiveTag.IsValid())
            	{
            		RemoveTag(Effect.WhileActiveTag);
            	}
                OnTimedEffectRemoved.Broadcast(Effect);
                ActiveTimedEffects.RemoveAt(i);
            }
        }
    }
}

void UStatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AnimRateTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}




