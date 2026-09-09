#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StatComponent.generated.h"

class UStatSet;     // forward declare
class APlayerState;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	Additive    UMETA(DisplayName = "Additive"),
	Multiplicative UMETA(DisplayName = "Multiplicative")
};

UENUM(BlueprintType)
enum class EStatTarget : uint8
{
	CurrentValue  UMETA(DisplayName = "Current Value"),
	MaxValue      UMETA(DisplayName = "Max Value"),
	Both          UMETA(DisplayName = "Both")
};

enum class EDamageOutcome
{
    Hit,
    CriticalHit,
    Dodged,
    Parried,
    Blocked
};

USTRUCT(BlueprintType)
struct FStat
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float BaseValue = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PermanentBase = 0.f; // Base + stat points only (no item bonuses)

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float PermanentMax = 100.f;  // 👈 put this before MaxValue

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float MaxValue = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CurrentValue = 100.f;

	int32 PointsSpent = 0;
	
	FStat() = default;

	FStat(float InBase)
		: BaseValue(InBase),
		  PermanentBase(InBase),
		  PermanentMax(InBase),
		  MaxValue(InBase),
		  CurrentValue(InBase)
	{}
};


USTRUCT(BlueprintType)
struct FStatModifier
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag StatTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EModifierType ModifierType = EModifierType::Additive;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EStatTarget Target = EStatTarget::CurrentValue;

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
		, InstigatorStatComp(nullptr)
		, CritMultiplier(1.f)
		, bForceCrit(false)
	{}

	// Base damage amount
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Magnitude = 0.f;

	// Type of damage (Physical, Fire, Magic, etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag DamageType;

	// Optional: override default instigator Stats for this hit
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	class UStatComponent* InstigatorStatComp = nullptr;

	// Optional: apply custom multipliers
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CritMultiplier = 1.f;

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
	FGameplayTag StatTag; // Target Stat

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
	UStatComponent* InstigatorStatComp = nullptr;

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

namespace StatTags
{
	static const FGameplayTag Health      = FGameplayTag::RequestGameplayTag("Stat.Health");
	static const FGameplayTag Damage      = FGameplayTag::RequestGameplayTag("Stat.Damage");
	static const FGameplayTag Armor       = FGameplayTag::RequestGameplayTag("Stat.Armor");
	static const FGameplayTag CritChance  = FGameplayTag::RequestGameplayTag("Stat.CritChance");
	static const FGameplayTag CritDamage  = FGameplayTag::RequestGameplayTag("Stat.CritDamage");
	static const FGameplayTag LifeSteal   = FGameplayTag::RequestGameplayTag("Stat.LifeSteal");
}

// 🔔 Delegate type: fires whenever an Stat changes
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnStatChanged, FGameplayTag, StatTag, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDeath, UStatComponent*, Instigator);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimedEffectAdded, FTimedEffect, Effect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTimedEffectRemoved, FTimedEffect, Effect);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTimedEffectStackChanged, FName, EffectID, int32, NewStackCount, float, Duration);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnTagChangedBP);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnTagChanged, FGameplayTag /*Tag*/, bool /*bAdded*/);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDamageTaken, float, Damage, Enum, EDamageOutcome);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);


UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class WOW_API UStatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStatComponent();

	//Cached references
	UPROPERTY()
	AActor* CachedActor;

	UPROPERTY()
	ARFPlayerState* CachedPlayerState;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stats")
	URFStatSet* StatSet;
	

	//Xp settings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LevelScaling")
	float LevelUpGrowthFactor = 0.1f; // +10% max XP per level

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="LevelScaling")
	int32 SpendablePointsPerLevel = 1.; // Ponts granted per levelup

	// Current player level
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Stats")
	int32 CurrentLevel = 1;

	UPROPERTY()
	TMap<FGameplayTag, int32> ItemModifiedStats;



protected:
	
	virtual void BeginPlay() override;
	void RebuildResistanceCache();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FGameplayTag, FStat> Stats;

	// --- Damagetype to resistances cache ---
	UPROPERTY()
	TMap<FGameplayTag, float> DamageTagToResistanceCache;

	UPROPERTY(BlueprintReadWrite, Category="Stats|TimedEffects")
	TArray<FTimedEffect> ActiveTimedEffects;
	
	// Tags currently owned by this component
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gameplay|Tags")
	FGameplayTagContainer OwnedTags;

public:
	
	// Apply a modifier to an Stat
	UFUNCTION(BlueprintCallable, Category="Stats", meta=(ToolTip="Applies a modifier to an Stat. Current, Max Or Both Should be used on Stats Like Health,Stamina etc. For flat Stats use Both"))
	void ApplyStatModifier(const FStatModifier& Modifier);
	void AddItemModifierTag(const FGameplayTag& Tag);
	void RemoveItemModifierTag(const FGameplayTag& Tag);

	// C++ only, not Blueprint-exposed
	FStat* GetStatRef(FGameplayTag StatTag);
	
	// Get the current value of an Stat
	UFUNCTION(BlueprintCallable, Category="Stats")
	float GetStatValue(FGameplayTag StatTag, EStatTarget Target = EStatTarget::CurrentValue) const;

	UFUNCTION(BlueprintCallable, Category="Stats")
	int32 GetPointsSpentOnStat(FGameplayTag StatTag) const;

	UFUNCTION(BlueprintCallable, Category="Stats")
	int32 ApplyPointToStat(const FGameplayTag& StatTag, int32 Amount, EStatTarget Target);

	//Clear all ItemModifiers
	void ClearItemModifiers();

	UFUNCTION(BlueprintCallable, Category="XP")
	void GrantExperience(float Amount);



	// Update Stats (clamp values)
	UFUNCTION(BlueprintCallable)
	void UpdateStats(bool bClampToMax /*= true*/);

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
	UFUNCTION(BlueprintCallable, Category="Stats")
	void ApplyDamage(const TArray<FDamageInfo>& DamageList);


	UFUNCTION(BlueprintCallable, Category="Gameplay|Stats")
	void ApplyPeriodicEffect(
		FGameplayTag StatTag,
		float Magnitude,
		float Duration,
		float TickInterval,
		EModifierType ModifierType,
		UStatComponent* InstigatorRFGC,
		bool bIsDOTorHOT,
		FName EffectID = NAME_None,     // Optional ID for UI/datatable
		FGameplayTag DamageTypeTag = FGameplayTag(), // optional, default empty
		FGameplayTag WhileActiveTag = FGameplayTag() // optional, default empty used for eg.State.Burning
	);

	UFUNCTION(BlueprintCallable, Category="Stats|TimedEffects")
	void RemovePeriodicEffectsByTag(FGameplayTag StatTag);

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
	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnStatChanged OnStatChanged;
	
	UPROPERTY(BlueprintAssignable, Category="Stats")
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

	UPROPERTY(BlueprintAssignable, Category="Stats")
	FOnLevelUp OnLevelUp;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsDead = false;


	
	
};
