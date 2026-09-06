// Fill out your copyright notice in the Description page of Project Settings.
// WBP_HealthbarBase.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WBP_HealthbarBase.generated.h"

class URFGameplayComponent;

UCLASS()
class RAVENSFALL2_API UWBP_HealthbarBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Called when HUD assigns this widget to an enemy */
	UFUNCTION(BlueprintNativeEvent, Category="Healthbar")
	void InitializeWithComponent(URFGameplayComponent* InComponent);
	virtual void InitializeWithComponent_Implementation(URFGameplayComponent* InComponent);

	/** Called when returning to pool / unbinding */
	UFUNCTION(BlueprintNativeEvent, Category="Healthbar")
	void Cleanup();
	virtual void Cleanup_Implementation();
	
	UFUNCTION(BlueprintNativeEvent, Category = "Healthbar")
	void SetHealthBarScale(float InScale);

protected:
	/** Keep reference to bound component */
	UPROPERTY(BlueprintReadOnly, Category="Healthbar")
	URFGameplayComponent* BoundComponent;
};

