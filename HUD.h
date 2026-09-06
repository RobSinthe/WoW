// RFHUD.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RFHUD.generated.h"

class UUserWidget;
class UCanvasPanel;
class ARFAICharacterBase; // your enemy base class
class UWBP_HealthbarBase;    // forward declare your healthbar widget

UCLASS()
class RAVENSFALL2_API ARFHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Register an enemy with the HUD manager */
	UFUNCTION(BlueprintCallable, Category="HUD|Enemies")
	void RegisterEnemy(ARFAICharacterBase* Enemy);
	UFUNCTION(BlueprintCallable, Category="HUD|Enemies")
	void RegisterBoss(ARFAICharacterBase* Enemy);
	UFUNCTION(BlueprintCallable, Category="HUD|Enemies")
	void UnregisterEnemy(ARFAICharacterBase* Enemy);
	UFUNCTION(BlueprintCallable, Category="HUD|Enemies")
	void UnregisterBoss();

	UFUNCTION(BlueprintCallable, Category="UI")
	URFHUDWidgetBase* GetHUDWidget() const;

protected:
	/** Widget class for the healthbar */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> HealthbarWidgetClass;
	
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UUserWidget> BossbarWidgetClass;

	/** Main HUD container (instanced in Blueprint) */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URFHUDWidgetBase> HUDWidgetClass;
	
	UPROPERTY()
	ARFAICharacterBase* ActiveBoss;


private:
	/** The HUD root widget */
	URFHUDWidgetBase* HUDWidget;

	/** Pool of healthbar widgets */
	TMap<TWeakObjectPtr<ARFAICharacterBase>, UUserWidget*> ActiveHealthbars;
	

	/** Registered enemies */
	TArray<TWeakObjectPtr<ARFAICharacterBase>> Enemies;

	/** Pool of unused healthbars */
	TArray<UUserWidget*> HealthbarPool;
	

	/** Pool size to pre-create */
	UPROPERTY(EditDefaultsOnly, Category="UI")
	int32 PoolSize = 10;

	// Time accumulator for occlusion updates
	float TimeSinceLastOcclusionCheck = 0.f;

	// How often to run occlusion traces (seconds)
	UPROPERTY(EditAnywhere, Category="UI")
	float OcclusionCheckInterval = 0.1f;

	// Round-robin index so we don’t check all enemies at once
	int32 OcclusionCheckIndex = 0;

	// Cache last occlusion state for each enemy
	TMap<TWeakObjectPtr<ARFAICharacterBase>, bool> CachedOcclusionStates;

	/** Utility */
	void UpdateHealthbars();
};
