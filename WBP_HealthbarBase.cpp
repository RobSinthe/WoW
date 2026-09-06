// Fill out your copyright notice in the Description page of Project Settings.


#include "WBP_HealthbarBase.h"
#include "RFGameplayComponent.h"

void UWBP_HealthbarBase::InitializeWithComponent_Implementation(URFGameplayComponent* InComponent)
{
	BoundComponent = InComponent;
	// Default behavior: just store the reference
	// In BP you can override this and bind to events
}

void UWBP_HealthbarBase::Cleanup_Implementation()
{
	// Default behavior: clear ref
	BoundComponent = nullptr;
	// In BP you can unbind delegates here
}

void UWBP_HealthbarBase::SetHealthBarScale_Implementation(float InScale)
{
}
