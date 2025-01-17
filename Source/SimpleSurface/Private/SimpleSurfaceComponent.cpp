﻿#include "SimpleSurfaceComponent.h"

#include "GameFramework/Actor.h"
#include "Components/MeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

DEFINE_LOG_CATEGORY(LogSimpleSurface);

void USimpleSurfaceComponent::DestroyComponent(const bool bPromoteChildren)
{
	TryRestoreMaterials();
	Super::DestroyComponent(bPromoteChildren);
}

void USimpleSurfaceComponent::SetParameter_Color(const FColor& InColor)
{
	this->Color = InColor;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_Glow(const float& InGlow)
{
	this->Glow = InGlow;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_ShininessRoughness(const float& InValue)
{
	this->ShininessRoughness = InValue;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_WaxinessMetalness(const float& InValue)
{
	this->WaxinessMetalness = InValue;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_Texture(UTexture* InTexture)
{
	this->Texture = InTexture;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_TextureIntensity(const float& InValue)
{
	this->TextureIntensity = InValue;
	ApplyParametersToMaterial();
}

void USimpleSurfaceComponent::SetParameter_TextureScale(const float& InValue)
{
	this->TextureScale = InValue;
	ApplyParametersToMaterial();
}

// Sets default values for this component's properties
USimpleSurfaceComponent::USimpleSurfaceComponent(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer),
	CapturedMeshComponentCount(0)
{
	PrimaryComponentTick.bCanEverTick = true;
	bTickInEditor = true;

	bAutoActivate = true;
	bWantsInitializeComponent = true;

	// TODO: Use a soft reference instead?  Unclear whether this tightly binds the plugin to the material somehow...
	static ConstructorHelpers::FObjectFinder<UMaterialInstance> MaterialFinder(
		TEXT("/SimpleSurface/Materials/MI_SimpleSurface.MI_SimpleSurface"));

	if (MaterialFinder.Succeeded() && !BaseMaterial)
	{
		BaseMaterial = MaterialFinder.Object;
	}
}

void USimpleSurfaceComponent::ApplyAll() const
{
	if (SimpleSurfaceMaterial)
	{
		ApplyParametersToMaterial();
		ApplyMaterialToMeshes();
	}
}

void USimpleSurfaceComponent::Activate(bool bReset)
{
	CaptureMaterials();
	ApplyAll();
	Super::Activate(bReset);
}

void USimpleSurfaceComponent::Deactivate()
{
	TryRestoreMaterials();
	Super::Deactivate();
}

void USimpleSurfaceComponent::InitializeSharedMID()
{
	UE_LOG(LogSimpleSurface, Verbose, TEXT("Initializing shared MID with outer %s (%p)"), *GetName(), this)

	// When duplicating actors, we must ensure that duplicated SimpleSurfaceComponents get their own instance of the SimpleSurfaceMaterial.
	if (!SimpleSurfaceMaterial || SimpleSurfaceMaterial.GetOuter() != this)
	{
		SimpleSurfaceMaterial = UMaterialInstanceDynamic::Create(BaseMaterial.Get(), this, TEXT("SimpleSurfaceMaterial"));
	}
}

void USimpleSurfaceComponent::ApplyParametersToMaterial() const
{
	check(SimpleSurfaceMaterial.Get())
	SimpleSurfaceMaterial->SetVectorParameterValue("Color", Color);
	SimpleSurfaceMaterial->SetScalarParameterValue("Glow", Glow);
	SimpleSurfaceMaterial->SetScalarParameterValue("Waxiness / Metalness", WaxinessMetalness);
	SimpleSurfaceMaterial->SetScalarParameterValue("Shininess / Roughness", ShininessRoughness);

	SimpleSurfaceMaterial->SetTextureParameterValue("Texture", Texture.Get());
	SimpleSurfaceMaterial->SetScalarParameterValue("Texture Intensity", TextureIntensity);
	SimpleSurfaceMaterial->SetScalarParameterValue("Texture Scale", TextureScale);
}

void USimpleSurfaceComponent::ApplyMaterialToMeshes() const
{
	if (!GetOwner())
	{
		return;
	}

	TArray<UMeshComponent*> MeshComponents;
	GetOwner()->GetComponents<UMeshComponent>(MeshComponents);

	check(SimpleSurfaceMaterial.Get())

	for (auto MeshComponent : MeshComponents)
	{
		for (auto i = 0; i < MeshComponent->GetNumMaterials(); i++)
		{
			MeshComponent->SetMaterial(i, SimpleSurfaceMaterial.Get());
		}
	}
}

ComponentMaterialMap USimpleSurfaceComponent::CreateComponentMaterialMap() const
{
	if (!GetOwner())
	{
		return {};
	}
	
	ComponentMaterialMap ResultMap;
	TArray<TObjectPtr<UMeshComponent>> AllMeshComponents;
	GetOwner()->GetComponents<UMeshComponent>(AllMeshComponents);
	
	for (const auto& MeshComponent : AllMeshComponents)
	{
		TMap<int32, TObjectPtr<UMaterialInterface>> MaterialMap;
		for (int32 i = 0; i < MeshComponent->GetNumMaterials(); i++)
		{
			TObjectPtr<UMaterialInterface> MatInterface = MeshComponent->GetMaterial(i);
			MaterialMap.Add(i, MatInterface);
		}
		ResultMap.Add(MeshComponent, MaterialMap);
	}

	return ResultMap;
}

void USimpleSurfaceComponent::UpdateComponentMaterialMap(ComponentMaterialMap& InOutMap) const
{
	if (!GetOwner())
	{
		return;		
	}

	TArray<UMeshComponent*, TInlineAllocator<32>> CurrentComponentsArray;
	GetOwner()->GetComponents<UMeshComponent>(CurrentComponentsArray);
	TSet<TObjectPtr<UMeshComponent>> CurrentComponentsSet;
	Algo::Transform(CurrentComponentsArray, CurrentComponentsSet, [](UMeshComponent* x) { return TObjectPtr(*x); });

	TSet<TObjectPtr<UMeshComponent>> OldKeys;
	InOutMap.GetKeys(OldKeys);

	auto NewComponents = CurrentComponentsSet.Difference(OldKeys);
	auto MissingComponents = OldKeys.Difference(CurrentComponentsSet);
	auto ExistingComponents = CurrentComponentsSet.Intersect(OldKeys);
	
	// Add new components
	for (const auto& NewComponent : NewComponents)
	{
		auto MaterialsBySlot = TMap<int32, TObjectPtr<UMaterialInterface>>();
		for (int32 i = 0; i < NewComponent->GetNumMaterials(); i++)
		{
			MaterialsBySlot.Add(i, NewComponent->GetMaterial(i));
		}
		InOutMap.Add(NewComponent, MaterialsBySlot);
	}

	// Remove old components
	for (const auto& MissingComponent : MissingComponents)
	{
		InOutMap.Remove(MissingComponent);
	}

	// Update existing components
	for (const auto& ExistingComponent : ExistingComponents)
	{
		auto& MaterialsBySlot = InOutMap[ExistingComponent];
		MaterialsBySlot.Reset();
		for (int32 i = 0; i < ExistingComponent->GetNumMaterials(); i++)
		{
			UMaterialInterface* ExistingMaterial = ExistingComponent->GetMaterial(i);
			if (!ExistingMaterial->IsA(SimpleSurfaceMaterial.GetClass()))
			{
				MaterialsBySlot.Add(i, ExistingMaterial);
			}
		}
	}
}

TArray<int32> USimpleSurfaceComponent::GetIndexPath(USceneComponent& Component)
{
	TArray<int32> IndexPath;
	auto Parent = Component.GetAttachParent();
	while (Parent)
	{
		auto Index = Parent->GetAttachChildren().Find(&Component);
		if (Index == INDEX_NONE)
		{
			break;
		}
		IndexPath.Insert(Index, 0);
		Parent = Parent->GetAttachParent();
	}
	return IndexPath;	
}

void USimpleSurfaceComponent::CaptureMaterials()
{
	if (!GetOwner() || !SimpleSurfaceMaterial)
	{
		return;
	}	
	
	// Update our records of all mesh components' current materials.
	TArray<TObjectPtr<UMeshComponent>> AllMeshComponents;
	GetOwner()->GetComponents<UMeshComponent>(AllMeshComponents);
	for (const auto& MeshComponent : AllMeshComponents)
	{
		if (!MeshComponent.Get())
		{
			CapturedMeshCatalog.Remove(MeshComponent);
			continue;
		}

		if (auto FoundCatalogRecord = CapturedMeshCatalog.Find(MeshComponent))
		{
			FoundCatalogRecord->UpdateRecord(*MeshComponent);			
		}
		else
		{
			CapturedMeshCatalog.Add(MeshComponent, FMeshCatalogRecord(*MeshComponent, { SimpleSurfaceMaterial.GetClass() }));
		}
	}
}

void USimpleSurfaceComponent::TryRestoreMaterials()
{
	if (!GetOwner())
	{
		return;
	}

	for (auto& ComponentToCatalogRecordKvp : CapturedMeshCatalog)
	{
		auto const &MeshComponent = ComponentToCatalogRecordKvp.Key;
		auto const &CatalogRecord = ComponentToCatalogRecordKvp.Value;

		// Start by clearing all override materials, including SimpleSurface.
		MeshComponent->EmptyOverrideMaterials();

		// Now restore captured materials.
		if (auto SafeComponent = MeshComponent.Get())
		{
			CatalogRecord.ApplyMaterials(*SafeComponent);
		}
		else
		{
			// No point keeping the record if the MeshComponent no longer exists.
			CapturedMeshCatalog.Remove(MeshComponent);
		}
	}	
}

void USimpleSurfaceComponent::InitializeComponent()
{
	UE_LOG(LogSimpleSurface, Verbose, TEXT(__FUNCSIG__))

	Super::InitializeComponent();
}

bool USimpleSurfaceComponent::MonitorForChanges(bool bForceUpdate)
{
	if (!GetOwner())
	{
		return false;
	}
	
	bool bChangeOccurred = false;

	TArray<UMeshComponent*, TInlineAllocator<32>> CurrentMeshComponents;
	GetOwner()->GetComponents<UMeshComponent>(CurrentMeshComponents);
	int32 CurrentMeshComponentCount = CurrentMeshComponents.Num();
	
	// Has the number of mesh components changed?
	if (CurrentMeshComponentCount != CapturedMeshComponentCount)
	{
		CapturedMeshComponentCount = CurrentMeshComponentCount;
		bChangeOccurred = true;
	}

	// Are there any materials in use that aren't SimpleSurface?
	// This indicates that a mesh has changed, and the new mesh has more material slots than the old mesh.
	for (auto Component : CurrentMeshComponents)
	{
		if (!Component->HasOverrideMaterials())
		{
			continue;
		}

		for (int32 i = 0; i < Component->GetNumMaterials(); i++)
		{
			if (!Component->GetMaterial(i)->IsA(SimpleSurfaceMaterial.GetClass()))
			{
				bChangeOccurred = true;
				break;
			}
		}

		if (bChangeOccurred)
		{
			break;
		}
	}
	
	return bChangeOccurred;
}

void USimpleSurfaceComponent::OnRegister()
{
	InitializeSharedMID();

	if (!GetOwner())
	{
		return;
	}

	CaptureMaterials();

	// Initialize/reset the buffers for subsequent monitoring.
	MonitorForChanges(true);
	
	// Calling ApplyAll() here ensures that all UMeshComponents on this actor that may already be using a SimpleSurfaceMaterial are using *this* component's instance of the SimpleSurfaceMaterial.
	// This is important following an actor duplication; we can't the duplicate's UMeshComponents referencing the original's SimpleSurfaceMaterial. 
	ApplyAll();
	
	Super::OnRegister();
}

void USimpleSurfaceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                            FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UE_LOG(LogSimpleSurface, Verbose, TEXT("%hs: responding to dirty flag by applying parameters to materials."), __FUNCSIG__)
	ApplyParametersToMaterial();
	
	if (MonitorForChanges())
	{
		UE_LOG(LogSimpleSurface, Verbose, TEXT("%hs: Change in mesh components or materials detected.  Recapturing materials and re-applying surface."), __FUNCSIG__)

		// Re-capture the most up-to-date component->materials maps.
		CaptureMaterials();

		// Re-apply SimpleSurface to all material slots.
		ApplyAll();
	}
}
