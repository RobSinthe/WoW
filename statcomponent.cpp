#include "RFGameplayComponent.h"
#include "RFAction.h"
#include "RFAttributeSet.h"
#include "ActionEventData.h"
#include "Engine/World.h"
#include "RFPlayerState.h"
#include "RFInventoryComponent.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"

URFGameplayComponent::URFGameplayComponent(): CachedActor(nullptr), CachedPlayerState(nullptr), CachedController(nullptr), CachedMesh(nullptr),
                                              AttributeSet(nullptr)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// Default Attributes --- Gets overridden by RFAttributeSet values if valid! --- 
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Health")), FAttribute(100.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Poise")), FAttribute(100.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Energy")), FAttribute(100.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Stamina")), FAttribute(100.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Oxygen")), FAttribute(100.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Damage")), FAttribute(20.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Attribute.Armor")), FAttribute(10.f));

	// Default Resistances
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Fire")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Frost")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Lightning")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Poison")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Shadow")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Slash")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Blunt")), FAttribute(0.f));
	Attributes.Add(FGameplayTag::RequestGameplayTag(FName("Resistance.Physical.Piercing")), FAttribute(0.f));
}

void URFGameplayComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AttributeSet)
	{
		for (const FTaggedAttribute& Entry : AttributeSet->Attributes)
		{
			FAttribute NewAttr;
			NewAttr.BaseValue = Entry.BaseValue;
			NewAttr.MaxValue = Entry.MaxValue;   // <-- uses your data asset value
			NewAttr.PermanentMax = Entry.MaxValue;
			NewAttr.CurrentValue = Entry.BaseValue; // optional — can set to Base or Max

			Attributes.Add(Entry.Tag, NewAttr);
		}
	}

	// Initialize attributes (permanent max, modifiers, clamp)
	UpdateAttributes(false);

	// Cache owner refs
	RefreshCachedRefs();

	// Grant all startup actions
	for (TSubclassOf<URFAction> ActionClass : StartupActions)
	{
		GrantAction(ActionClass);
	}
}


// -- Actions Section ---

void URFGameplayComponent::RebuildResistanceCache()
{
	DamageTagToResistanceCache.Empty();

	for (const auto& Pair : Attributes)
	{
		const FGameplayTag& AttrTag = Pair.Key;

		// Only attributes starting with "Resistance"
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

URFAction* URFGameplayComponent::GetActionByTag(FGameplayTag ActionTag) const
{
	for (URFAction* Action : GrantedActions)
	{
		if (Action && Action->ActionTag == ActionTag)
		{
			return Action;
		}
	}
	return nullptr;
}

URFAction* URFGameplayComponent::GrantAction(TSubclassOf<URFAction> ActionClass)
{
	if (!*ActionClass) return nullptr;

	// Prevent duplicates
	for (URFAction* Action : GrantedActions)
	{
		if (Action && Action->GetClass() == *ActionClass)
		{
			return Action;
		}
	}

	URFAction* NewAction = NewObject<URFAction>(this, ActionClass);
	if (NewAction)
	{
		GrantedActions.Add(NewAction);

		// Initialize once at grant time (does NOT trigger full activation logic)
		NewAction->InitializeActionInfo(CachedActor, CachedMesh, CachedController, this);
	}

	return NewAction;
}

bool URFGameplayComponent::RemoveAction(TSubclassOf<URFAction> ActionClass)
{
	if (!*ActionClass) return false;

	for (int32 i = GrantedActions.Num() - 1; i >= 0; --i)
	{
		URFAction* Action = GrantedActions[i];
		if (Action && Action->GetClass() == *ActionClass)
		{
			GrantedActions.RemoveAt(i);
			ActiveActions.Remove(Action);
			return true;
		}
	}
	return false;
}

void URFGameplayComponent::SendActionEvent(const FActionEventData& EventData)
{
	for (URFAction* Action : GrantedActions)
	{
		if (!Action) continue;

		// Only consider the action that matches this event
		if (Action->ActionTag != EventData.EventTag)
		{
			continue;
		}

		// Blocked tags check
		if (OwnedTags.HasAny(Action->BlockedTags))
		{
			UE_LOG(LogTemp, Warning, TEXT("ActionEvent blocked by tags!"));
			return;
		}

		// Required tags check
		if (!Action->RequiredTags.IsEmpty() && !OwnedTags.HasAll(Action->RequiredTags))
		{
			UE_LOG(LogTemp, Warning, TEXT("ActionEvent blocked for missing required tags!"));
			return;
		}

		const bool bWasInactive = !ActiveActions.Contains(Action);
		if (bWasInactive)
		{
			for (const FGameplayTag& CancelTag : Action->CancelActionsWithTags)
			{
				EndAction(CancelTag);
			}

			InitActionInfo(Action);
			ActiveActions.Add(Action);
			Action->ActivateAction();
		}

		Action->HandleActionEvent(EventData);

		// If only one action should respond to a given tag, stop here:
		return;
	}
}

bool URFGameplayComponent::TryActivateAction(FGameplayTag ActionTag)
{
    URFAction* Action = GetActionByTag(ActionTag);
	UE_LOG(LogTemp, Log, TEXT("First step check: Requesting start on action: %s"), *ActionTag.ToString());
    if (!Action)
    {
    	UE_LOG(LogTemp, Log, TEXT("URFAction::ActivateAction - ActionCheck (action not valid!!!)"));
	    return false;
    }
    const float TimeNow = GetWorld()->GetTimeSeconds();

    // ---- Cooldown ----
    if (TimeNow - Action->LastActivationTime < Action->CooldownTime)
    {
        UE_LOG(LogTemp, Warning, TEXT("Action %s on cooldown"), *ActionTag.ToString());
        return false;
    }

    // ---- Tag checks ----
    if (OwnedTags.HasAny(Action->BlockedTags))
    {
        UE_LOG(LogTemp, Warning, TEXT("Action %s blocked by tags"), *ActionTag.ToString());
        return false;
    }
    if (!OwnedTags.HasAll(Action->RequiredTags))
    {
        UE_LOG(LogTemp, Warning, TEXT("Action %s missing required tags"), *ActionTag.ToString());
        return false;
    }

    // ---- Attribute cost check ----
    for (const auto& Pair : Action->AttributeCosts)
    {
        const FGameplayTag& Attribute = Pair.Key;
        float Cost = Pair.Value;

        float CurrentValue = GetAttributeValue(Attribute, EAttributeTarget::CurrentValue);
        if (CurrentValue < Cost)
        {
            UE_LOG(LogTemp, Warning, TEXT("Not enough %s to activate %s"),
                *Attribute.ToString(), *ActionTag.ToString());
            return false;
        }
    }

    // ---- Apply costs ----
	for (const auto& Pair : Action->AttributeCosts)
	{
		const FGameplayTag& Attribute = Pair.Key;
		float Cost = Pair.Value;

		FAttributeModifier Modifier;
		Modifier.AttributeTag = Attribute;
		Modifier.Magnitude    = -Cost;
		Modifier.Target       = EAttributeTarget::CurrentValue;
		Modifier.ModifierType = EModifierType::Additive;

		ApplyAttributeModifier(Modifier);
	}

    // ---- Activate Action ----
    Action->LastActivationTime = TimeNow;

	// ---- Cancel other actions ----
	for (const FGameplayTag& CancelTag : Action->CancelActionsWithTags)
	{
   	 	EndAction(CancelTag);
	}

		// Add action to active actions list
	if (!ActiveActions.Contains(Action))
	{
		InitActionInfo(Action);
		ActiveActions.Add(Action);
	}
	
	// 🔹 If this action’s input tag is currently held, notify it right away
	if (CurrentlyHeldInputs.Contains(ActionTag)) 
	{
		Action->OnInputPressed();
	}

    return true;
}

void URFGameplayComponent::HandleInputPressed(FGameplayTag InputTag)
{
	const float Now = GetWorld()->GetTimeSeconds();
	CurrentlyHeldInputs.Add(InputTag, Now);

	TArray<URFAction*> ActionsCopy = ActiveActions;

	for (URFAction* Action : ActionsCopy)
	{
		if (Action && Action->ActionTag == InputTag)
		{
			Action->InputPressedStartTime = Now;
			Action->OnInputPressed();
		}
	}
}

void URFGameplayComponent::HandleInputReleased(FGameplayTag InputTag)
{
	const float Now = GetWorld()->GetTimeSeconds();

	TArray<URFAction*> ActionsCopy = ActiveActions;

	for (URFAction* Action : ActionsCopy)
	{
		if (Action && Action->ActionTag == InputTag)
		{
			float HeldTime = 0.0f;

			if (Action->InputPressedStartTime > 0.0f)
			{
				HeldTime = Now - Action->InputPressedStartTime;
			}

			Action->OnInputReleased(HeldTime);
		}
	}

	CurrentlyHeldInputs.Remove(InputTag);
}

void URFGameplayComponent::InitActionInfo(URFAction* Action)
{
	UE_LOG(LogTemp, Log, TEXT("URFGameplayComponent::InitActionInfo triggered"));

	if (!Action) return;

	// Ensure cached refs are valid (handles delayed possession, etc.)
	RefreshCachedRefs();

	// Pass cached refs to the action
	Action->ActivateAction();
}

void URFGameplayComponent::RefreshCachedRefs()
{
	if (!CachedActor)
		CachedActor = GetOwner();

	if (ARFPlayerState* PS = Cast<ARFPlayerState>(CachedActor))
	{
		CachedPlayerState = PS; // ✅ Store it

		if (APawn* Pawn = PS->GetPawn())
		{
			CachedActor = Pawn;
			CachedController = Pawn->GetController();

			if (ACharacter* Char = Cast<ACharacter>(Pawn))
			{
				CachedMesh = Char->GetMesh();
			}
		}
	}
	else if (ACharacter* Char = Cast<ACharacter>(CachedActor))
	{
		CachedController = Char->GetController();
		CachedMesh = Char->GetMesh();
	}
	else if (APawn* Pawn = Cast<APawn>(CachedActor))
	{
		CachedController = Pawn->GetController();
	}
}

bool URFGameplayComponent::EndAction(FGameplayTag ActionTag)
{
	URFAction* Action = GetActionByTag(ActionTag);
	if (!Action) return false;

	Action->EndAction();          // let the action handle its own cleanup
	EndActionInstance(Action);    // remove from ActiveActions

	return true;
}

void URFGameplayComponent::EndActionInstance(URFAction* Action)
{
	if (!Action) return;

	ActiveActions.RemoveSingle(Action);
}

void URFGameplayComponent::ApplyAttributeModifier(const FAttributeModifier& Modifier)
{
	if (FAttribute* Attr = Attributes.Find(Modifier.AttributeTag))
	{
		float OldCurrent = Attr->CurrentValue;
		float OldMax     = Attr->MaxValue;

		// Current / Both
		if (Modifier.Target == EAttributeTarget::CurrentValue || Modifier.Target == EAttributeTarget::Both)
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
		if (Modifier.Target == EAttributeTarget::MaxValue || Modifier.Target == EAttributeTarget::Both)
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
			OnAttributeChanged.Broadcast(Modifier.AttributeTag, Attr->CurrentValue);
		}
		if (!FMath::IsNearlyEqual(OldMax, Attr->MaxValue))
		{
			OnAttributeChanged.Broadcast(Modifier.AttributeTag, Attr->MaxValue);
		}
		// --- Rebuild resistance cache if this attribute is a resistance ---
		if (Modifier.AttributeTag.GetTagName().ToString().Contains(TEXT("Resistance.")))
		{
			RebuildResistanceCache();
		}
	}
}


//Tracking Item AttributeModifiers
void URFGameplayComponent::AddItemModifierTag(const FGameplayTag& Tag)
{
	int32& Count = ItemModifiedAttributes.FindOrAdd(Tag);
	Count++;
}

void URFGameplayComponent::RemoveItemModifierTag(const FGameplayTag& Tag)
{
	if (int32* Count = ItemModifiedAttributes.Find(Tag))
	{
		(*Count)--;

		if (*Count <= 0)
		{
			ItemModifiedAttributes.Remove(Tag);
		}
	}
}
//////////////////////////

FAttribute* URFGameplayComponent::GetAttributeRef(FGameplayTag AttributeTag)
{
	return Attributes.Find(AttributeTag); // returns FAttribute*, might be nullptr
}

float URFGameplayComponent::GetAttributeValue(FGameplayTag AttributeTag, EAttributeTarget Target) const
{
	if (const FAttribute* Attr = Attributes.Find(AttributeTag))
	{
		switch (Target)
		{
		case EAttributeTarget::CurrentValue: return Attr->CurrentValue;
		case EAttributeTarget::MaxValue:     return Attr->MaxValue;
		case EAttributeTarget::Both:         return Attr->CurrentValue; // default to current
		}
	}
	return 0.f;
}

int32 URFGameplayComponent::GetPointsSpentOnAttribute(FGameplayTag AttributeTag) const
{
	if (const FAttribute* Attr = Attributes.Find(AttributeTag))
	{
		return Attr->PointsSpent;
	}
	return 0.f;
}


// --- UpdateAttributes and apply levelpoints --- 
void URFGameplayComponent::UpdateAttributes(bool bClampToMax /*= true*/)
{
	const FGameplayTag XPTag = FGameplayTag::RequestGameplayTag(FName("Attribute.XP"));
	const FGameplayTag PointsTag = FGameplayTag::RequestGameplayTag(FName("Attribute.SpendablePoints"));

	for (auto& Pair : Attributes)
	{
		FGameplayTag Tag = Pair.Key;
		FAttribute& Attr = Pair.Value;

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
			OnAttributeChanged.Broadcast(Tag, Attr.CurrentValue);
		}

		if (!FMath::IsNearlyEqual(OldMax, Attr.MaxValue) ||
			!FMath::IsNearlyEqual(OldPermanent, Attr.PermanentMax))
		{
			OnAttributeChanged.Broadcast(Tag, Attr.MaxValue);
		}

		// --- Rebuild resistance cache if this is a resistance attribute ---
		if (Tag.ToString().Contains(TEXT("Resistance.")))
		{
			RebuildResistanceCache();
		}
	}
}


// --- Spend Leveling points into Attributes ---- 
int32 URFGameplayComponent::ApplyPointToAttribute(
	const FGameplayTag& AttributeTag, int32 Amount, EAttributeTarget Target)
{
	// --- Check SpendablePoints first ---
	if (FAttribute* Spendable = Attributes.Find(FGameplayTag::RequestGameplayTag(FName("Attribute.SpendablePoints"))))
	{
		if (Spendable->CurrentValue < Amount)
		{
			// Not enough points to spend
			return 0;
		}

		// Subtract points from the pool
		Spendable->CurrentValue -= Amount;
		OnAttributeChanged.Broadcast(FGameplayTag::RequestGameplayTag(FName("Attribute.SpendablePoints")), Spendable->CurrentValue);
	}
	else
	{
		// No SpendablePoints attribute found, can't continue
		return 0;
	}

	// --- Apply points to target attribute ---
	if (FAttribute* Attr = Attributes.Find(AttributeTag))
	{
		Attr->PointsSpent += Amount;

		// Update permanent max (base calculation — item bonuses will be reapplied afterward)
		Attr->PermanentMax = Attr->BaseValue * FMath::Sqrt(static_cast<float>(Attr->PointsSpent + 1));

		// Runtime max = permanent (no active modifiers applied yet)
		Attr->MaxValue = Attr->PermanentMax;

		// Clamp or adjust current value depending on target
		switch (Target)
		{
		case EAttributeTarget::CurrentValue:
			Attr->CurrentValue = FMath::Clamp(Attr->CurrentValue, 0.f, Attr->MaxValue);
			break;

		case EAttributeTarget::MaxValue:
			// leave CurrentValue untouched
				break;

		case EAttributeTarget::Both:
			Attr->CurrentValue = Attr->MaxValue;
			break;
		}

		// Broadcast attribute change
		OnAttributeChanged.Broadcast(AttributeTag, Attr->CurrentValue);

		// --- Reapply item bonuses after attribute point allocation ---
		if (CachedPlayerState)
		{
			if (URFInventoryComponent* InvComp = CachedPlayerState->FindComponentByClass<URFInventoryComponent>())
			{
				InvComp->ReapplyAllItemAttributes();
			}
		}

		return Attr->PointsSpent;
	}

	return 0;
}

void URFGameplayComponent::ClearItemModifiers()
{
	if (ItemModifiedAttributes.Num() == 0)
	{
		return;
	}

	// Iterate through all tags currently modified by one or more items
	for (auto It = ItemModifiedAttributes.CreateIterator(); It; ++It)
	{
		const FGameplayTag& Tag = It.Key();
		int32 Count = It.Value();

		if (Count <= 0)
		{
			continue; // Skip any invalid entries (shouldn't normally happen)
		}

		if (FAttribute* Attr = Attributes.Find(Tag))
		{
			// Reset attribute to its permanent (non-item) value
			Attr->MaxValue = Attr->PermanentMax;
			Attr->CurrentValue = FMath::Clamp(Attr->CurrentValue, 0.f, Attr->MaxValue);

			// Notify listeners/UI
			OnAttributeChanged.Broadcast(Tag, Attr->CurrentValue);
		}
	}

	// ✅ Clear all entries, since we’ll reapply current equipment afterward
	ItemModifiedAttributes.Empty();
}


void URFGameplayComponent::GrantExperience(float Amount)
{
    if (Amount <= 0.f)
        return;

    static const FGameplayTag XPTag = FGameplayTag::RequestGameplayTag(FName("Attribute.XP"));
    static const FGameplayTag SpendablePointsTag = FGameplayTag::RequestGameplayTag(FName("Attribute.SpendablePoints"));

    // Ensure LevelUpGrowthFactor has a sane default (e.g. 0.1 = +10%)
    if (LevelUpGrowthFactor <= 0.f)
        LevelUpGrowthFactor = 0.1f;

    // Find or create XP attribute
    FAttribute* XPAttr = Attributes.Find(XPTag);
    if (!XPAttr)
    {
        FAttribute NewXP;
        NewXP.CurrentValue = 0.f;
        NewXP.MaxValue = 100.f;
        NewXP.BaseValue = 0.f;
        NewXP.PermanentMax = 100.f;
        Attributes.Add(XPTag, NewXP);
        XPAttr = Attributes.Find(XPTag);
    }

    FAttribute& XP = *XPAttr;

    XP.CurrentValue += Amount;
    OnAttributeChanged.Broadcast(XPTag, XP.CurrentValue);

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
        FAttributeModifier RewardModifier;
        RewardModifier.AttributeTag = SpendablePointsTag;
        RewardModifier.Target = EAttributeTarget::CurrentValue;
        RewardModifier.ModifierType = EModifierType::Additive;
        RewardModifier.Magnitude = SpendablePointsPerLevel;
        ApplyAttributeModifier(RewardModifier);

        // Fire Blueprint-native event
        OnLevelUp.Broadcast(CurrentLevel);
    }

    if (SafetyCounter >= MaxIterations)
    {
        UE_LOG(LogTemp, Error, TEXT("GrantExperience hit safety limit — possible XP config error (XP.MaxValue=%.2f, XP.Current=%.2f)"), XP.MaxValue, XP.CurrentValue);
    }

    if (bLeveledUp)
    {
        OnAttributeChanged.Broadcast(XPTag, XP.CurrentValue);
    }
}



// --- Fix so Default is Damage.Physical ---
float URFGameplayComponent::GetResistanceForDamage(const FGameplayTag& DamageTag)
{
	// Try to find the resistance in the cache
	for (const auto& Pair : DamageTagToResistanceCache)
	{
		if (DamageTag.MatchesTag(Pair.Key))
			return Pair.Value;
	}

	return 0.f;
}


float URFGameplayComponent::CalculateDamageReduction(float Value, float CapAtValue, float MaxReduction) const
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

void URFGameplayComponent::ApplyDamage(const TArray<FDamageInfo>& DamageList)
{
    if (DamageList.Num() == 0)
        return;

    float TotalDamage = 0.f;
	float TotalPoiseDamage = 0.f;
    float Armor = GetAttributeValue(RFAttributeTags::Armor);

    // --- Instigator stats (one instigator per DamageList) ---
    URFGameplayComponent* InstigatorGC = DamageList[0].InstigatorRFGC;
    FInstigatorStats Stats;

    if (InstigatorGC)
    {
        Stats.BaseDamage       = InstigatorGC->GetAttributeValue(RFAttributeTags::Damage);
        Stats.CritChance       = InstigatorGC->GetAttributeValue(RFAttributeTags::CritChance);
        Stats.CritMultiplier   = InstigatorGC->GetAttributeValue(RFAttributeTags::CritDamage);
        Stats.LifeStealPercent = InstigatorGC->GetAttributeValue(RFAttributeTags::LifeSteal);
    }
    else
    {
        Stats.BaseDamage       = 1.f;
        Stats.CritChance       = 0.f;
        Stats.CritMultiplier   = 1.f;
        Stats.LifeStealPercent = 0.f;
    }

    // --- Roll crit once for the whole hit ---
    bool bCrit = false;
    if (Stats.CritChance > 0.f && FMath::FRand() <= Stats.CritChance)
    {
        bCrit = true;
    }
    // --- Loop over damage parts ---
    for (const FDamageInfo& Info : DamageList)
    {
        bool bIsDoT = Info.DamageType.GetTagName().ToString().Contains("DoT");

        // Base damage
        float IncomingDamage = bIsDoT ? Info.Magnitude : Info.Magnitude * Stats.BaseDamage;

        // Armor reduction (physical only)
        float ArmorReduction = 0.f;
        if (Info.DamageType.MatchesTag(FGameplayTag::RequestGameplayTag(FName("Damage.Physical"))))
        {
            ArmorReduction = CalculateDamageReduction(Armor, ArmorCapValue, DamageReductionCap);
        }
        float AfterArmor = IncomingDamage * (1.f - ArmorReduction);

        // Resistance reduction
        float ResistanceValue = GetResistanceForDamage(Info.DamageType);
        float ResistReduction = CalculateDamageReduction(ResistanceValue, ResistCapValue, DamageReductionCap);
        float AfterResist = AfterArmor * (1.f - ResistReduction);

        // Apply crit if rolled
        float FinalDamage = bCrit ? AfterResist * Stats.CritMultiplier : AfterResist;

        // Clamp and add
        FinalDamage = FMath::Max(FinalDamage, 0.f);
        TotalDamage += FinalDamage;
    	
    	// --- Poise ---
    	if (Info.bApplyPoiseDamage && !bIsDoT)
    	{
    		float PoiseDamage = FinalDamage * Info.PoiseMultiplier;
    		TotalPoiseDamage += PoiseDamage;
    	}

        // Debug
        if (bDebugDamage)
        {
            UE_LOG(LogTemp, Warning, TEXT(
                "DamageInfo: %.2f | Incoming: %.2f | Armor: %.2f | ArmorRed: %.2f | AfterArmor: %.2f | "
                "Resist: %.2f | ResistRed: %.2f | AfterResist: %.2f | Crit: %s | LifeSteal: %.2f | FinalDamage: %.2f | TotalDamage: %.2f| TotalPoiseDamage: %.2f"),
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
                TotalPoiseDamage
            );
        }

        // Lifesteal (only for non-DoTs)
        if (!bIsDoT && Stats.LifeStealPercent > 0.f && InstigatorGC)
        {
            FAttribute* InstigatorHealthPtr = InstigatorGC->GetAttributeRef(RFAttributeTags::Health);
            if (InstigatorHealthPtr)
            {
                FAttribute& InstigatorHealth = *InstigatorHealthPtr;
                float Heal = FinalDamage * Stats.LifeStealPercent;
                InstigatorHealth.CurrentValue = FMath::Clamp(
                    InstigatorHealth.CurrentValue + Heal, 0.f, InstigatorHealth.MaxValue);
                InstigatorGC->OnAttributeChanged.Broadcast(RFAttributeTags::Health, InstigatorHealth.CurrentValue);
            }
        }
    }

    // --- Apply total damage to target ---
    FAttribute* HealthPtr = GetAttributeRef(RFAttributeTags::Health);
    if (HealthPtr)
    {
        FAttribute& Health = *HealthPtr;
        Health.CurrentValue = FMath::Clamp(Health.CurrentValue - TotalDamage, 0.f, Health.MaxValue);
        OnAttributeChanged.Broadcast(RFAttributeTags::Health, Health.CurrentValue);

        if (Health.CurrentValue <= 0.f && !bIsDead)
        {
            bIsDead = true;
            OnDeath.Broadcast(InstigatorGC);
        }
    }
	// Apply POISE HERE :::: IMPLEMENT THIS!!!
	if (TotalPoiseDamage > 0.f)
	{
		ApplyPoiseDamage(TotalPoiseDamage, InstigatorGC);
	}
//
    // --- Fire delegate once per hit ---
    OnDamageTaken.Broadcast(TotalDamage, bCrit);
}



void URFGameplayComponent::ApplyPoiseDamage(float Amount, URFGameplayComponent* InstigatorGC)
{
	FAttribute* PoisePtr = GetAttributeRef(RFAttributeTags::Poise);
	if (!PoisePtr)
		return;

	FAttribute& Poise = *PoisePtr;

	Poise.CurrentValue = FMath::Clamp(Poise.CurrentValue - Amount, 0.f, Poise.MaxValue);
	OnAttributeChanged.Broadcast(RFAttributeTags::Poise, Poise.CurrentValue);

	// Retriggerable restore delay
	GetWorld()->GetTimerManager().ClearTimer(PoiseRegenDelayHandle);
	GetWorld()->GetTimerManager().SetTimer(
		PoiseRegenDelayHandle,
		this,
		&URFGameplayComponent::RestorePoise,
		PoiseRegenDelay,
		false
	);

	if (Poise.CurrentValue <= 0.f && !bPoiseBroken)
	{
		bPoiseBroken = true;
		OnPoiseBreakBP.Broadcast();
		
		FActionEventData EventData;
		EventData.EventTag = PoiseBreakEventTag;
		EventData.Instigator = InstigatorGC ? InstigatorGC->GetOwner() : nullptr;
		EventData.HitResult = FHitResult();
		
	SendActionEvent(EventData);
	}
}

void URFGameplayComponent::RestorePoise()
{
	FAttribute* PoisePtr = GetAttributeRef(RFAttributeTags::Poise);
	if (!PoisePtr)
		return;

	FAttribute& Poise = *PoisePtr;

	Poise.CurrentValue = Poise.MaxValue;
	bPoiseBroken = false;

	OnAttributeChanged.Broadcast(RFAttributeTags::Poise, Poise.CurrentValue);
}


void URFGameplayComponent::ApplyPeriodicEffect(
    FGameplayTag AttributeTag,
    float Magnitude,
    float Duration,
    float TickInterval,
    EModifierType ModifierType,
    URFGameplayComponent* InstigatorRFGC,
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
            Effect.InstigatorRFGC   = InstigatorRFGC;

            OnTimedEffectStackChanged.Broadcast(Effect.EffectID, Effect.StackCount, Effect.TimeRemaining);
            return;
        }
    }

    // Create new effect
    FTimedEffect NewEffect;
    NewEffect.EffectID        = EffectID;
    NewEffect.AttributeTag    = AttributeTag;
    NewEffect.DamageTypeTag   = DamageTypeTag;
    NewEffect.Magnitude       = Magnitude;
    NewEffect.ModifierType    = ModifierType;
    NewEffect.Duration        = Duration;
    NewEffect.TickInterval    = TickInterval;
    NewEffect.TimeRemaining   = Duration;
    NewEffect.TimeUntilNextTick = TickInterval > 0.f ? TickInterval : 0.f;
    NewEffect.bIsDOTorHOT     = bIsDOTorHOT;
    NewEffect.InstigatorRFGC   = InstigatorRFGC;
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
        	DamageInfo.InstigatorRFGC = InstigatorRFGC;

        	TArray<FDamageInfo> DamageArray;
        	DamageArray.Add(DamageInfo);

        	ApplyDamage(DamageArray);
        }
        else
        {
            // Buff/debuff applied instantly
            FAttributeModifier Mod;
            Mod.AttributeTag   = AttributeTag;
            Mod.Magnitude      = Magnitude;
            Mod.ModifierType   = ModifierType;
            Mod.Target         = EAttributeTarget::Both;
            ApplyAttributeModifier(Mod);
        }
    }

    // Broadcast new effect
    OnTimedEffectAdded.Broadcast(NewEffect);
	if (WhileActiveTag.IsValid())
	{
		AddTag(WhileActiveTag);
	}
}



void URFGameplayComponent::RemovePeriodicEffectsByTag(FGameplayTag AttributeTag)
{
	for (int32 i = ActiveTimedEffects.Num() - 1; i >= 0; --i)
	{
		FTimedEffect& Effect = ActiveTimedEffects[i];

		if (Effect.AttributeTag == AttributeTag)
		{
			if (!Effect.bIsDOTorHOT)
			{
				FAttributeModifier InverseMod;
				InverseMod.AttributeTag = Effect.AttributeTag;
				InverseMod.Target = EAttributeTarget::Both;
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

				ApplyAttributeModifier(InverseMod);
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

void URFGameplayComponent::AddTag(FGameplayTag Tag)
{
	if (!OwnedTags.HasTag(Tag))
	{
		OwnedTags.AddTag(Tag);

		// Broadcast to listeners
		OnTagChanged.Broadcast(Tag, true);
		OnTagChangedBP.Broadcast();     // For Blueprints

		UE_LOG(LogTemp, Log, TEXT("URFGameplayComponent::AddTag - Added Tag: %s"), *Tag.ToString());
	}
}

void URFGameplayComponent::RemoveTag(FGameplayTag Tag)
{
	if (OwnedTags.HasTag(Tag))
	{
		OwnedTags.RemoveTag(Tag);

		// Broadcast to listeners
		OnTagChanged.Broadcast(Tag, false);
		OnTagChangedBP.Broadcast();     // For Blueprints

		UE_LOG(LogTemp, Log, TEXT("URFGameplayComponent::RemoveTag - Removed Tag: %s"), *Tag.ToString());
	}
}

bool URFGameplayComponent::HasTag(FGameplayTag Tag) const
{
	return OwnedTags.HasTag(Tag);
}

void URFGameplayComponent::ApplySlomoFX(USkeletalMeshComponent* Mesh, float NewRate, float Duration)
{
	if (!Mesh || !GetWorld())
	{
		return;
	}

	// If we’re applying to a new mesh (or first time), capture original rate
	if (TargetMesh.Get() != Mesh)
	{
		OriginalRate = Mesh->GlobalAnimRateScale;
		TargetMesh = Mesh;
	}

	// Set slowed animation rate
	Mesh->GlobalAnimRateScale = NewRate;

	// Retriggerable delay: clear and restart
	GetWorld()->GetTimerManager().ClearTimer(AnimRateTimerHandle);

	if (Duration <= 0.f)
	{
		ResetAnimRate();
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(
		AnimRateTimerHandle,
		this,
		&URFGameplayComponent::ResetAnimRate,
		Duration,
		false
	);
}

void URFGameplayComponent::ResetAnimRate()
{
	if (USkeletalMeshComponent* Mesh = TargetMesh.Get())
	{
		Mesh->GlobalAnimRateScale = OriginalRate;
	}

	TargetMesh = nullptr;
}


void URFGameplayComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
            	DamageInfo.InstigatorRFGC = Effect.InstigatorRFGC;

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

                FAttributeModifier Modifier;
                Modifier.AttributeTag = Effect.AttributeTag;
                Modifier.Magnitude = EffectiveMagnitude;
                Modifier.ModifierType = Effect.ModifierType;
                Modifier.Target = EAttributeTarget::Both;

                ApplyAttributeModifier(Modifier);

                Effect.TimeUntilNextTick += Effect.TickInterval;
            }

            // Expire ALL stacks when timer runs out
            if (Effect.TimeRemaining <= 0.f)
            {
                // Revert total stacked effect
                FAttributeModifier InverseMod;
                InverseMod.AttributeTag = Effect.AttributeTag;
                InverseMod.ModifierType = Effect.ModifierType;
                InverseMod.Target = EAttributeTarget::Both;

                if (Effect.ModifierType == EModifierType::Additive)
                {
                    InverseMod.Magnitude = -Effect.Magnitude * Effect.StackCount;
                }
                else // Multiplicative
                {
                    float TotalFactor = FMath::Pow(1.f + Effect.Magnitude, Effect.StackCount);
                    InverseMod.Magnitude = (1.f / TotalFactor) - 1.f;
                }

                ApplyAttributeModifier(InverseMod);
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

void URFGameplayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AnimRateTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}




