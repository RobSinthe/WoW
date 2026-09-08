#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RFGameplayComponent.generated.h"

class URFAction;		 // forward declare
class URFAttributeSet;     // forward declare
struct FActionEventData; // forward declaration
class ARFPlayerState;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	Additive    UMETA(DisplayName = "Additive"),
	Multiplicative UMETA(DisplayName = "Multiplicative")
};

UENUM(BlueprintType)
enum class EAttributeTarget : uint8
{
	CurrentValue  UMETA(DisplayName = "Current Value"),
	MaxValue      UMETA(DisplayName = "Max Value"),
	Both          UMETA(DisplayName = "Both")
};

USTRUCT(BlueprintType)
struct FAttribute
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BaseValue = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PermanentBase = 0.f; // Base + attribute points only (no item bonuses)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PermanentMax = 100.f;  // 👈 put this before MaxValue

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxValue = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CurrentValue = 100.f;

	int32 PointsSpent = 0;
	
	FAttribute() = default;

	FAttribute(float InBase)
		: BaseValue(InBase),
		  PermanentBase(InBase),
		  PermanentMax(InBase),
		  MaxValue(InBase),
		  CurrentValue(InBase),
		  PointsSpent(0)
	{}
};


USTRUCT(BlueprintType)
struct FAttributeModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag AttributeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EModifierType ModifierType = EModifierType::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAttributeTarget Target = EAttributeTarget::CurrentValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Magnitude = 0.f;
};

USTRUCT(BlueprintType)
struct FDamageInfo
{
	GENERATED_BODY()

	FDamageInfo()
		: Magnitude(0.f)
		, DamageType(FGameplayTag::RequestGameplayTag(TEXT("Damage.Physical")))
		, InstigatorRFGC(nullptr)
		, CritMultiplier(1.f)
		, PoiseMultiplier(0.f)
		, bApplyPoiseDamage(true)
		, bForceCrit(false)
	{}

	// Base damage amount
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Magnitude = 0.f;

	// Type of damage (Physical, Fire, Magic, etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag DamageType;

	// Optional: override default instigator attributes for this hit
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	class URFGameplayComponent* InstigatorRFGC = nullptr;

	// Optional: apply custom multipliers
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CritMultiplier = 1.f;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PoiseMultiplier = 0.f;

	// Optional but very useful
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bApplyPoiseDamage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bForceCrit = false;
};

USTRUCT(BlueprintType)
struct FTimedEffect
{
	GENERATED_BODY()

	// Unique ID for this effect, used for UI, datatables, etc.
	UPROPERTY(BlueprintReadWrite, Category="TimedEffect")
	FName EffectID = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag AttributeTag; // Target attribute

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag DamageTypeTag; // Damage type (optional)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Magnitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Duration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TickInterval = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TimeRemaining = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TimeUntilNextTick = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsDOTorHOT = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	URFGameplayComponent* InstigatorRFGC = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EModifierType ModifierType = EModifierType::Additive; // pick your default

	// New: stack count
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	int32 StackCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag WhileActiveTag;
};

USTRUCT(BlueprintType)
struct FDamageResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	float TotalDamage = 0.f;

	UPROPERTY(BlueprintReadOnly)
	bool bCrit = false;
};

USTRUCT(BlueprintType)
struct FInstigatorStats
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float BaseDamage = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float CritChance = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float CritMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float LifeStealPercent = 0.f;

	FInstigatorStats() = default;

	FInstigatorStats(float InBaseDamage, float InCritChance, float InCritMultiplier, float InLifeStealPercent)
		: BaseDamage(InBaseDamage)
		, CritChance(InCritChance)
		, CritMultiplier(InCritMultiplier)
		, LifeStealPercent(InLifeStealPercent)
	{}
};

namespace RFAttributeTags
{
	static const FGameplayTag Health      = FGameplayTag::RequestGameplayTag("Attribute.Health");
	static const FGameplayTag Poise      = FGameplayTag::RequestGameplayTag("Attribute.Poise");
	static const FGameplayTag Damage      = FGameplayTag::RequestGameplayTag("Attribute.Damage");
	static const FGameplayTag Armor       = FGameplayTag::RequestGameplayTag("Attribute.Armor");
	static const FGameplayTag CritChance  = FGameplayTag::RequestGameplayTag("Attribute.CritChance");
	static const FGameplayTag CritDamage  = FGameplayTag::RequestGameplayTag("Attribute.CritDamage");
	static const FGameplayTag LifeSteal   = FGameplayTag::RequestGameplayTag("Attribute.LifeSteal");
}

// 🔔 Delegate type: fires whenever an attribute changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAttributeChanged, FGameplayTag, AttributeTag, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, URFGameplayComponent*, Instigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimedEffectAdded, FTimedEffect, Effect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimedEffectRemoved, FTimedEffect, Effect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTimedEffectStackChanged, FName, EffectID, int32, NewStackCount, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTagChangedBP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPoiseBreakBP);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTagChanged, FGameplayTag /*Tag*/, bool /*bAdded*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageTaken, float, Damage, bool, bCrit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class RAVENSFALL2_API URFGameplayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URFGameplayComponent();

	//Cached references
	UPROPERTY()
	AActor* CachedActor;

	UPROPERTY()
	ARFPlayerState* CachedPlayerState;

	UPROPERTY()
	AController* CachedController;

	UPROPERTY()
	USkeletalMeshComponent* CachedMesh;

	// --- Action Registry ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Actions")
	TArray<TSubclassOf<URFAction>> StartupActions;
	
	UPROPERTY(VisibleAnywhere, Instanced, Category="Actions")
	TArray<URFAction*> GrantedActions;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Actions")
	TArray<URFAction*> ActiveActions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attributes")
	URFAttributeSet* AttributeSet;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise Settings")
	float PoiseRegenDelay = 2.0f;
	
	//Calculate the max healthvalue * PoiseThreshold and set the poise attribute value to that (MAYBE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise Settings")
	float PoiseThreshold = 0.25f;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Poise Settings")
	FGameplayTag PoiseBreakEventTag;

	UFUNCTION(BlueprintCallable, Category="Actions")
	URFAction* GrantAction(TSubclassOf<URFAction> ActionClass);

	UFUNCTION(BlueprintCallable, Category="Actions")
	bool RemoveAction(TSubclassOf<URFAction> ActionClass);

	UFUNCTION(BlueprintCallable)
	bool TryActivateAction(FGameplayTag ActivationTag);

	UFUNCTION(BlueprintCallable, Category="Actions")
	void SendActionEvent(const FActionEventData& EventData);

	//Initialize Avatar info etc.
	void InitActionInfo(URFAction* Action);
	void RefreshCachedRefs();

	//Xp settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LevelScaling")
	float LevelUpGrowthFactor = 0.1f; // +10% max XP per level

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LevelScaling")
	int32 SpendablePointsPerLevel = 5.; // Ponts granted per levelup

	// Current player level
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Attributes")
	int32 CurrentLevel = 1;

	UFUNCTION(BlueprintCallable, Category="Actions")
	bool EndAction(FGameplayTag ActionTag);

	UPROPERTY()
	TMap<FGameplayTag, int32> ItemModifiedAttributes;
	
	UFUNCTION(BlueprintCallable, Category="Gameplay")
	void ApplySlomoFX(USkeletalMeshComponent* Mesh, float NewRate, float Duration);


protected:
	
	virtual void BeginPlay() override;
	void RebuildResistanceCache();
	URFAction* GetActionByTag(FGameplayTag ActionTag) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, FAttribute> Attributes;

	// --- Damagetype to resistances cache ---
	UPROPERTY()
	TMap<FGameplayTag, float> DamageTagToResistanceCache;

	UPROPERTY(BlueprintReadWrite, Category="Attributes|TimedEffects")
	TArray<FTimedEffect> ActiveTimedEffects;
	
	// Tags currently owned by this component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Tags")
	FGameplayTagContainer OwnedTags;

	// Tracks inputs currently held: Tag → PressTime
	UPROPERTY(VisibleAnywhere, Category="Actions|Input")
	TMap<FGameplayTag, float> CurrentlyHeldInputs;
	
	// Reset Slomo effect
	void ResetAnimRate();
	FTimerHandle AnimRateTimerHandle;
	TWeakObjectPtr<USkeletalMeshComponent> TargetMesh;

	float OriginalRate = 1.0f;


private:
	UPROPERTY()
	TMap<FGameplayTag, URFAction*> ActionMap;

	UPROPERTY()
	TMap<FGameplayTag, float> ActionCooldowns;
	
	FTimerHandle PoiseRegenDelayHandle;
	bool bPoiseBroken = false;

public:

	// Called by your input binding when an input is pressed
	UFUNCTION(BlueprintCallable, Category="Action|Input")
	void HandleInputPressed(FGameplayTag InputTag);

	// Called by your input binding when an input is released
	UFUNCTION(BlueprintCallable, Category="Action|Input")
	void HandleInputReleased(FGameplayTag InputTag);

	UFUNCTION()
	void EndActionInstance(URFAction* Action);
	
	// Apply a modifier to an attribute
	UFUNCTION(BlueprintCallable, Category="Attributes", meta=(ToolTip="Applies a modifier to an attribute. Current, Max Or Both Should be used on Attributes Like Health,Stamina etc. For flat attributes use Both"))
	void ApplyAttributeModifier(const FAttributeModifier& Modifier);
	void AddItemModifierTag(const FGameplayTag& Tag);
	void RemoveItemModifierTag(const FGameplayTag& Tag);

	// C++ only, not Blueprint-exposed
	FAttribute* GetAttributeRef(FGameplayTag AttributeTag);
	
	// Get the current value of an attribute
	UFUNCTION(BlueprintCallable, Category="Attributes")
	float GetAttributeValue(FGameplayTag AttributeTag, EAttributeTarget Target = EAttributeTarget::CurrentValue) const;

	UFUNCTION(BlueprintCallable, Category="Attributes")
	int32 GetPointsSpentOnAttribute(FGameplayTag AttributeTag) const;

	UFUNCTION(BlueprintCallable, Category="Attributes")
	int32 ApplyPointToAttribute(const FGameplayTag& AttributeTag, int32 Amount, EAttributeTarget Target);

	//Clear all ItemModifiers
	void ClearItemModifiers();

	UFUNCTION(BlueprintCallable, Category="XP")
	void GrantExperience(float Amount);



	// Update attributes (clamp values)
	UFUNCTION(BlueprintCallable)
	void UpdateAttributes(bool bClampToMax /*= true*/);

	float GetResistanceForDamage(const FGameplayTag& DamageTag);

	// --- Damage Reduction Settings ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage Reduction")
	float ArmorCapValue = 1000.f;  // Value of Armor needed to reach strong diminishing returns

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage Reduction")
	float ResistCapValue = 1000.f; // Value of Resistance needed to reach strong diminishing returns

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Damage Reduction")
	float DamageReductionCap = 0.8f; // Max reduction (80% = 0.8)

	// --- Debug ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Debug")
	bool bDebugDamage = true;
    
	// --- Calculate Damage Reduction --- 
	float CalculateDamageReduction(float Value, float CapAtValue, float MaxReduction) const;
	
	// New function to apply damage
	UFUNCTION(BlueprintCallable, Category="Attributes")
	void ApplyDamage(const TArray<FDamageInfo>& DamageList);
	// PoiseManagement
	void ApplyPoiseDamage(float Amount, URFGameplayComponent* InstigatorGC);
	void RestorePoise();


	UFUNCTION(BlueprintCallable, Category="Gameplay|Attributes")
	void ApplyPeriodicEffect(
		FGameplayTag AttributeTag,
		float Magnitude,
		float Duration,
		float TickInterval,
		EModifierType ModifierType,
		URFGameplayComponent* InstigatorRFGC,
		bool bIsDOTorHOT,
		FName EffectID = NAME_None,     // Optional ID for UI/datatable
		FGameplayTag DamageTypeTag = FGameplayTag(), // optional, default empty
		FGameplayTag WhileActiveTag = FGameplayTag() // optional, default empty used for eg.State.Burning
	);

	UFUNCTION(BlueprintCallable, Category="Attributes|TimedEffects")
	void RemovePeriodicEffectsByTag(FGameplayTag AttributeTag);

	//Gameplay tags section ---

	UFUNCTION(BlueprintCallable, Category="Tags")
	void AddTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Tags")
	void RemoveTag(FGameplayTag Tag);

	UFUNCTION(BlueprintCallable, Category="Tags")
	bool HasTag(FGameplayTag Tag) const;

	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction);
	void EndPlay(EEndPlayReason::Type EndPlayReason);

	// 🔔 Delegate exposed to Blueprints
	UPROPERTY(BlueprintAssignable, Category="Attributes")
	FOnAttributeChanged OnAttributeChanged;
	
	UPROPERTY(BlueprintAssignable, Category="Attributes")
	FOnDeath OnDeath;

	UPROPERTY(BlueprintAssignable, Category="Dots/Hots")
	FOnTimedEffectAdded OnTimedEffectAdded;
	
	UPROPERTY(BlueprintAssignable, Category="Dots/Hots")
	FOnTimedEffectRemoved OnTimedEffectRemoved;

	UPROPERTY(BlueprintAssignable, Category="Dots/Hots")
	FOnTimedEffectStackChanged OnTimedEffectStackChanged;

	UPROPERTY(BlueprintAssignable, Category="Tags")
	FOnTagChangedBP OnTagChangedBP;
	
	UPROPERTY(BlueprintAssignable, Category="Tags")
	FOnPoiseBreakBP OnPoiseBreakBP;

	UPROPERTY(BlueprintAssignable, Category="Damage")
	FOnDamageTaken OnDamageTaken;
	
	FOnTagChanged OnTagChanged;

	UPROPERTY(BlueprintAssignable, Category="Attributes")
	FOnLevelUp OnLevelUp;
	

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsDead = false;


	
	
};
