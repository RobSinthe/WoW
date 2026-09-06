// RFHUD.cpp
#include "RFHUD.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h" // for GetViewportScale
#include "RFAICharacterBase.h" // your enemy class
#include "RFGameplayComponent.h"
#include "RFHUDWidgetBase.h"
#include "WBP_HealthbarBase.h"    // your BP healthbar widget (exported as class)

void ARFHUD::BeginPlay()
{
    Super::BeginPlay();

    if (HUDWidgetClass)
    {
        HUDWidget = CreateWidget<URFHUDWidgetBase>(GetWorld(), HUDWidgetClass);
        if (HUDWidget)
        {
            HUDWidget->AddToViewport();
        }
    }

    // Pre-create widgets for pooling
    if (HealthbarWidgetClass)
    {
        for (int32 i = 0; i < PoolSize; i++)
        {
            if (UUserWidget* HB = CreateWidget<UUserWidget>(GetWorld(), HealthbarWidgetClass))
            {
                HB->AddToViewport();
                HB->SetVisibility(ESlateVisibility::Collapsed);
                HealthbarPool.Add(HB);
            }
        }
    }
}

void ARFHUD::RegisterEnemy(ARFAICharacterBase* Enemy)
{
    if (!Enemy || Enemies.Contains(Enemy)) return;
    Enemies.Add(Enemy);

    UUserWidget* HB = nullptr;

    if (HealthbarPool.Num() > 0)
    {
        HB = HealthbarPool.Pop(); // take one from the pool
    }
    else if (HealthbarWidgetClass) // fallback: create new
    {
        HB = CreateWidget<UUserWidget>(GetWorld(), HealthbarWidgetClass);
        if (HB) HB->AddToViewport();
    }

    if (HB)
    {
        HB->SetVisibility(ESlateVisibility::Collapsed);

        if (UWBP_HealthbarBase* Healthbar = Cast<UWBP_HealthbarBase>(HB))
        {
            if (URFGameplayComponent* GC = Enemy->FindComponentByClass<URFGameplayComponent>())
            {
                Healthbar->InitializeWithComponent(GC);
            }
        }

        ActiveHealthbars.Add(Enemy, HB);
    }
}



void ARFHUD::UnregisterEnemy(ARFAICharacterBase* Enemy)
{
    Enemies.Remove(Enemy);

    if (UUserWidget** Found = ActiveHealthbars.Find(Enemy))
    {
        if (UUserWidget* HB = *Found)
        {
            // Optional: let the widget clean up its bindings
            if (UWBP_HealthbarBase* Healthbar = Cast<UWBP_HealthbarBase>(HB))
            {
                Healthbar->Cleanup(); // Blueprint event (unbind delegates)
            }

            HB->SetVisibility(ESlateVisibility::Collapsed);
            HealthbarPool.Add(HB); // return to pool
        }
        ActiveHealthbars.Remove(Enemy);
    }
}

void ARFHUD::RegisterBoss(ARFAICharacterBase* Boss)
{
    if (!HUDWidget || !HUDWidget->IsInViewport())
    {
        UE_LOG(LogTemp, Warning, TEXT("AddBossbar failed: HUDWidget not ready"));
        return;
    }
    if (!Boss || !BossbarWidgetClass) return;
    if (Boss == ActiveBoss) return;

    UnregisterBoss();

    ActiveBoss = Boss;

    UUserWidget* BossbarWidget =
        CreateWidget<UUserWidget>(GetWorld(), BossbarWidgetClass);

    if (!BossbarWidget) return;

    if (URFHUDWidgetBase* HUD = Cast<URFHUDWidgetBase>(HUDWidget))
    {
        HUD->AddBossbar(BossbarWidget);   // ✅ forces Construct
    }

    if (UWBP_HealthbarBase* Bossbar = Cast<UWBP_HealthbarBase>(BossbarWidget))
    {
        if (URFGameplayComponent* GC = Boss->FindComponentByClass<URFGameplayComponent>())
        {
            Bossbar->InitializeWithComponent(GC); // ✅ now safe
        }
    }
}

void ARFHUD::UnregisterBoss()
{
    if (URFHUDWidgetBase* HUD = Cast<URFHUDWidgetBase>(HUDWidget))
    {
        HUD->RemoveBossbar();
    }

    ActiveBoss = nullptr;
}

URFHUDWidgetBase* ARFHUD::GetHUDWidget() const
{
    return HUDWidget;
}

void ARFHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateHealthbars();
}

void ARFHUD::UpdateHealthbars()
{
    APlayerController* PC = GetOwningPlayerController();
    if (!PC) return;

    FVector CameraLoc;
    FRotator CameraRot;
    PC->GetPlayerViewPoint(CameraLoc, CameraRot);

    // Accumulate time
    TimeSinceLastOcclusionCheck += GetWorld()->GetDeltaSeconds();
    bool bDoOcclusionThisFrame = false;
    if (TimeSinceLastOcclusionCheck >= OcclusionCheckInterval)
    {
        bDoOcclusionThisFrame = true;
        TimeSinceLastOcclusionCheck = 0.f;
    }

    int32 EnemyCount = Enemies.Num();

    for (int32 i = 0; i < EnemyCount; i++)
    {
        TWeakObjectPtr<ARFAICharacterBase> Enemy = Enemies[i];
        if (!Enemy.IsValid()) continue;

        FVector WorldPos = Enemy->GetMesh()->GetSocketLocation("head") 
                         + FVector(0.f, 0.f, Enemy->HealthbarZOffset);

        FVector2D ScreenPos;
        UUserWidget* HB = ActiveHealthbars[Enemy];
        if (!HB) continue;

        const bool bOnScreen = UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(
            PC, WorldPos, ScreenPos, false);

        if (!bOnScreen)
        {
            HB->SetVisibility(ESlateVisibility::Collapsed);
            continue;
        }

        // Distance scaling
        const float Dist = FVector::Dist(CameraLoc, WorldPos);
        const float FullScaleDist = 250.f;
        const float MinScaleDist = 2000.f;
        const float MinScale = 0.6f;

        float Scale = 1.f;
        if (Dist > FullScaleDist)
        {
            const float Alpha = (Dist - FullScaleDist) / (MinScaleDist - FullScaleDist);
            Scale = FMath::Lerp(1.f, MinScale, FMath::Clamp(Alpha, 0.f, 1.f));
        }

        // Default to cached visibility if available
        bool bVisible = true;
        if (CachedOcclusionStates.Contains(Enemy))
        {
            bVisible = CachedOcclusionStates[Enemy];
        }

        // ✅ Update cache only when we actually trace this enemy
        if (bDoOcclusionThisFrame && (i % 3 == OcclusionCheckIndex))
        {
            FHitResult Hit;
            FCollisionQueryParams Params;
            Params.AddIgnoredActor(PC->GetPawn());
            Params.AddIgnoredActor(Enemy.Get());

            const bool bHit = GetWorld()->LineTraceSingleByChannel(
                Hit, CameraLoc, WorldPos, ECC_Visibility, Params);

            bVisible = !(bHit && Hit.GetActor() != Enemy.Get());

            CachedOcclusionStates.Add(Enemy, bVisible);
        }

        // Apply result
        HB->SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible
                                   : ESlateVisibility::Collapsed);
        HB->SetRenderTransformPivot(FVector2D(0.5f, 1.f));
        HB->SetRenderTranslation(ScreenPos);

        if (UWBP_HealthbarBase* Healthbar = Cast<UWBP_HealthbarBase>(HB))
        {
            Healthbar->SetHealthBarScale(Scale);
        }
    }

    // Rotate round-robin index
    if (bDoOcclusionThisFrame)
    {
        OcclusionCheckIndex = (OcclusionCheckIndex + 1) % 3;
    }
}





