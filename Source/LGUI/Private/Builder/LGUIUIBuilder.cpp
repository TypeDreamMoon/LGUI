// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Builder/LGUIUIBuilder.h"
#include "LGUI.h"
#include "Core/ActorComponent/UIItem.h"
#include "Core/ActorComponent/UIBaseRenderable.h"
#include "Core/ActorComponent/UIText.h"
#include "Core/ActorComponent/UISpriteBase.h"
#include "Core/ActorComponent/UITextureBase.h"
#include "Core/Actor/UIContainerActor.h"
#include "Core/Actor/UISpriteActor.h"
#include "Core/Actor/UITextActor.h"
#include "Core/Actor/UITextureActor.h"
#include "Interaction/UIButtonComponent.h"
#include "Layout/UIPanelLayoutBase.h"
#include "Layout/UIPanelLayout_HorizontalBox.h"
#include "Layout/UIPanelLayout_VerticalBox.h"
#include "Layout/UIPanelLayout_Overlay.h"
#include "Layout/UIPanelLayout_UniformGrid.h"
#include "Layout/UIPanelLayout_FlexibleGrid.h"
#include "Core/ActorComponent/LGUICanvas.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "LGUIBPLibrary.h"
#include "Core/LGUIScreenUISubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace LGUIBuilder
{

namespace Local
{
	UUIItem* GetRootUIItem(AActor* InActor)
	{
		return InActor != nullptr ? Cast<UUIItem>(InActor->GetRootComponent()) : nullptr;
	}
	void StretchToFillParent(UUIItem* InItem)
	{
		FUIAnchorData AnchorData;
		AnchorData.AnchorMin = FVector2D(0, 0);
		AnchorData.AnchorMax = FVector2D(1, 1);
		AnchorData.AnchoredPosition = FVector2D::ZeroVector;
		AnchorData.SizeDelta = FVector2D::ZeroVector;
		InItem->SetAnchorData(AnchorData);
	}
	template<class T>
	T* GetRootAs(AActor* InActor, const TCHAR* InSugarName)
	{
		T* Result = InActor != nullptr ? Cast<T>(InActor->GetRootComponent()) : nullptr;
		if (Result == nullptr)
		{
			UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] .%s() ignored: element root is not a %s."), InSugarName, *T::StaticClass()->GetName());
		}
		return Result;
	}
	void WarnSlotMismatch(const TCHAR* InSugarName, UUIPanelLayoutSlotBase* InSlot)
	{
		UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] .%s() ignored: parent layout slot is %s, which has no such setting.")
			, InSugarName, InSlot != nullptr ? *InSlot->GetClass()->GetName() : TEXT("null"));
	}
}

//--- common config ---

FUINode& FUINode::Name(const FString& InName)
{
	ElementName = InName;
	return *this;
}
FUINode& FUINode::Size(float InWidth, float InHeight)
{
	bHasExplicitGeometry = true;
	ActorConfigs.Add([InWidth, InHeight](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Item->SetWidth(InWidth);
				Item->SetHeight(InHeight);
			}
		});
	return *this;
}
FUINode& FUINode::AnchoredPosition(float InX, float InY)
{
	bHasExplicitGeometry = true;
	ActorConfigs.Add([InX, InY](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Item->SetAnchoredPosition(FVector2D(InX, InY));
			}
		});
	return *this;
}
FUINode& FUINode::Anchor(const FVector2D& InMin, const FVector2D& InMax)
{
	bHasExplicitGeometry = true;
	ActorConfigs.Add([InMin, InMax](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Item->SetHorizontalAnchorMinMax(FVector2D(InMin.X, InMax.X), true);
				Item->SetVerticalAnchorMinMax(FVector2D(InMin.Y, InMax.Y), true);
			}
		});
	return *this;
}
FUINode& FUINode::FillParent()
{
	bHasExplicitGeometry = true;
	ActorConfigs.Add([](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Local::StretchToFillParent(Item);
			}
		});
	return *this;
}
FUINode& FUINode::Pivot(const FVector2D& InPivot)
{
	ActorConfigs.Add([InPivot](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Item->SetPivot(InPivot);
			}
		});
	return *this;
}
FUINode& FUINode::Color(const FColor& InColor)
{
	ActorConfigs.Add([InColor](AActor* Actor)
		{
			if (auto Renderable = Local::GetRootAs<UUIBaseRenderable>(Actor, TEXT("Color")))
			{
				Renderable->SetColor(InColor);
			}
		});
	return *this;
}
FUINode& FUINode::RaycastTarget(bool bValue)
{
	ActorConfigs.Add([bValue](AActor* Actor)
		{
			if (auto Item = Local::GetRootUIItem(Actor))
			{
				Item->SetRaycastTarget(bValue);
			}
		});
	return *this;
}
FUINode& FUINode::Setup(TFunction<void(AActor*)> InFunc)
{
	ActorConfigs.Add(MoveTemp(InFunc));
	return *this;
}
FUINode& FUINode::WithComponent(TSubclassOf<UActorComponent> InClass, TFunction<void(UActorComponent*)> InConfig)
{
	if (InClass == nullptr)return *this;
	// the config is paired with THIS created instance at build time -- finding by class
	// would alias onto a pre-existing component of the same class (prefab roots, repeats)
	ExtraComponents.Add({ InClass, MoveTemp(InConfig) });
	return *this;
}

//--- type-specific sugar ---

FUINode& FUINode::Text(const FText& InText)
{
	ActorConfigs.Add([InText](AActor* Actor)
		{
			if (auto TextComp = Local::GetRootAs<UUIText>(Actor, TEXT("Text")))
			{
				TextComp->SetText(InText);
			}
		});
	return *this;
}
FUINode& FUINode::FontSize(float InSize)
{
	ActorConfigs.Add([InSize](AActor* Actor)
		{
			if (auto TextComp = Local::GetRootAs<UUIText>(Actor, TEXT("FontSize")))
			{
				TextComp->SetFontSize(InSize);
			}
		});
	return *this;
}
FUINode& FUINode::Font(ULGUIFontData_BaseObject* InFont)
{
	ActorConfigs.Add([InFont](AActor* Actor)
		{
			if (auto TextComp = Local::GetRootAs<UUIText>(Actor, TEXT("Font")))
			{
				TextComp->SetFont(InFont);
			}
		});
	return *this;
}
FUINode& FUINode::Sprite(ULGUISpriteData_BaseObject* InSprite, bool bSetNativeSize)
{
	ActorConfigs.Add([InSprite, bSetNativeSize](AActor* Actor)
		{
			if (auto SpriteComp = Local::GetRootAs<UUISpriteBase>(Actor, TEXT("Sprite")))
			{
				SpriteComp->SetSprite(InSprite, bSetNativeSize);
			}
		});
	return *this;
}
FUINode& FUINode::Texture(UTexture* InTexture)
{
	ActorConfigs.Add([InTexture](AActor* Actor)
		{
			if (auto TextureComp = Local::GetRootAs<UUITextureBase>(Actor, TEXT("Texture")))
			{
				TextureComp->SetTexture(InTexture);
			}
		});
	return *this;
}

//--- slot config ---

FUINode& FUINode::Padding(const FMargin& InPadding)
{
	SlotConfigs.Add([InPadding](UUIPanelLayoutSlotBase* Slot)
		{
			// every concrete slot type owns its own Padding property; route through the
			// known families instead of reflection to keep this fast and typo-proof
			if (auto HBoxSlot = Cast<UUIPanelLayout_HorizontalBox_Slot>(Slot)) { HBoxSlot->SetPadding(InPadding); }
			else if (auto VBoxSlot = Cast<UUIPanelLayout_VerticalBox_Slot>(Slot)) { VBoxSlot->SetPadding(InPadding); }
			else if (auto OverlaySlot = Cast<UUIPanelLayout_Overlay_Slot>(Slot)) { OverlaySlot->SetPadding(InPadding); }
			else if (auto GridSlot = Cast<UUIPanelLayout_UniformGrid_Slot>(Slot)) { GridSlot->SetPadding(InPadding); }
			else if (auto FlexSlot = Cast<UUIPanelLayout_FlexibleGrid_Slot>(Slot)) { FlexSlot->SetPadding(InPadding); }
			else { Local::WarnSlotMismatch(TEXT("Padding"), Slot); }
		});
	return *this;
}
FUINode& FUINode::HAlign(EHorizontalAlignment InAlign)
{
	SlotConfigs.Add([InAlign](UUIPanelLayoutSlotBase* Slot)
		{
			if (auto HBoxSlot = Cast<UUIPanelLayout_HorizontalBox_Slot>(Slot)) { HBoxSlot->SetHorizontalAlignment(InAlign); }
			else if (auto VBoxSlot = Cast<UUIPanelLayout_VerticalBox_Slot>(Slot)) { VBoxSlot->SetHorizontalAlignment(InAlign); }
			else if (auto OverlaySlot = Cast<UUIPanelLayout_Overlay_Slot>(Slot)) { OverlaySlot->SetHorizontalAlignment(InAlign); }
			else if (auto GridSlot = Cast<UUIPanelLayout_UniformGrid_Slot>(Slot)) { GridSlot->SetHorizontalAlignment(InAlign); }
			else if (auto FlexSlot = Cast<UUIPanelLayout_FlexibleGrid_Slot>(Slot)) { FlexSlot->SetHorizontalAlignment(InAlign); }
			else { Local::WarnSlotMismatch(TEXT("HAlign"), Slot); }
		});
	return *this;
}
FUINode& FUINode::VAlign(EVerticalAlignment InAlign)
{
	SlotConfigs.Add([InAlign](UUIPanelLayoutSlotBase* Slot)
		{
			if (auto HBoxSlot = Cast<UUIPanelLayout_HorizontalBox_Slot>(Slot)) { HBoxSlot->SetVerticalAlignment(InAlign); }
			else if (auto VBoxSlot = Cast<UUIPanelLayout_VerticalBox_Slot>(Slot)) { VBoxSlot->SetVerticalAlignment(InAlign); }
			else if (auto OverlaySlot = Cast<UUIPanelLayout_Overlay_Slot>(Slot)) { OverlaySlot->SetVerticalAlignment(InAlign); }
			else if (auto GridSlot = Cast<UUIPanelLayout_UniformGrid_Slot>(Slot)) { GridSlot->SetVerticalAlignment(InAlign); }
			else if (auto FlexSlot = Cast<UUIPanelLayout_FlexibleGrid_Slot>(Slot)) { FlexSlot->SetVerticalAlignment(InAlign); }
			else { Local::WarnSlotMismatch(TEXT("VAlign"), Slot); }
		});
	return *this;
}
FUINode& FUINode::SizeFill(float InRatio)
{
	SlotConfigs.Add([InRatio](UUIPanelLayoutSlotBase* Slot)
		{
			FSlateChildSize SizeRule;
			SizeRule.SizeRule = ESlateSizeRule::Fill;
			SizeRule.Value = InRatio;
			if (auto HBoxSlot = Cast<UUIPanelLayout_HorizontalBox_Slot>(Slot)) { HBoxSlot->SetSizeRule(SizeRule); }
			else if (auto VBoxSlot = Cast<UUIPanelLayout_VerticalBox_Slot>(Slot)) { VBoxSlot->SetSizeRule(SizeRule); }
			else { Local::WarnSlotMismatch(TEXT("SizeFill"), Slot); }
		});
	return *this;
}
FUINode& FUINode::SizeAuto()
{
	SlotConfigs.Add([](UUIPanelLayoutSlotBase* Slot)
		{
			FSlateChildSize SizeRule;
			SizeRule.SizeRule = ESlateSizeRule::Automatic;
			if (auto HBoxSlot = Cast<UUIPanelLayout_HorizontalBox_Slot>(Slot)) { HBoxSlot->SetSizeRule(SizeRule); }
			else if (auto VBoxSlot = Cast<UUIPanelLayout_VerticalBox_Slot>(Slot)) { VBoxSlot->SetSizeRule(SizeRule); }
			else { Local::WarnSlotMismatch(TEXT("SizeAuto"), Slot); }
		});
	return *this;
}
FUINode& FUINode::DesiredSize(float InWidth, float InHeight)
{
	SlotConfigs.Add([InWidth, InHeight](UUIPanelLayoutSlotBase* Slot)
		{
			Slot->SetDesiredSizeMode(EUIPanelLayoutSlotDesiredSizeMode::Manual);
			Slot->SetDesiredSize(FVector2D(InWidth, InHeight));
		});
	return *this;
}
FUINode& FUINode::DesiredSizeAuto()
{
	SlotConfigs.Add([](UUIPanelLayoutSlotBase* Slot)
		{
			Slot->SetDesiredSizeMode(EUIPanelLayoutSlotDesiredSizeMode::AutoFromContent);
		});
	return *this;
}
FUINode& FUINode::Cell(int32 InRow, int32 InColumn)
{
	SlotConfigs.Add([InRow, InColumn](UUIPanelLayoutSlotBase* Slot)
		{
			if (auto GridSlot = Cast<UUIPanelLayout_UniformGrid_Slot>(Slot))
			{
				GridSlot->SetRow(InRow);
				GridSlot->SetColumn(InColumn);
			}
			else if (auto FlexSlot = Cast<UUIPanelLayout_FlexibleGrid_Slot>(Slot))
			{
				FlexSlot->SetRow(InRow);
				FlexSlot->SetColumn(InColumn);
			}
			else { Local::WarnSlotMismatch(TEXT("Cell"), Slot); }
		});
	return *this;
}
FUINode& FUINode::IgnoreLayout(bool bIgnore)
{
	SlotConfigs.Add([bIgnore](UUIPanelLayoutSlotBase* Slot)
		{
			Slot->SetIgnoreLayout(bIgnore);
		});
	return *this;
}

//--- events ---

FUINode& FUINode::OnClick(UObject* InOwner, TFunction<void()> InCallback)
{
	ActorConfigs.Add([WeakOwner = TWeakObjectPtr<UObject>(InOwner), Callback = MoveTemp(InCallback)](AActor* Actor)
		{
			if (auto Button = Actor->FindComponentByClass<UUIButtonComponent>())
			{
				// weak binding: disarms automatically when the owner dies, so the captured
				// 'this' in the callback can never be called dangling
				if (UObject* Owner = WeakOwner.Get())
				{
					Button->RegisterClickEvent(FSimpleDelegate::CreateWeakLambda(Owner, [Callback]() { Callback(); }));
				}
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] .OnClick() ignored on \"%s\": no UIButtonComponent (use Button() or a button prefab)."), *Actor->GetName());
			}
		});
	return *this;
}
FUINode& FUINode::OnClick(TFunction<void()> InCallback)
{
	ActorConfigs.Add([Callback = MoveTemp(InCallback)](AActor* Actor)
		{
			if (auto Button = Actor->FindComponentByClass<UUIButtonComponent>())
			{
				Button->RegisterClickEvent(Callback);
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] .OnClick() ignored on \"%s\": no UIButtonComponent (use Button() or a button prefab)."), *Actor->GetName());
			}
		});
	return *this;
}

//--- build ---

FBuiltUI FUINode::Build(UWorld* InWorld, USceneComponent* InParent)const
{
	FBuiltUI Result;
	if (InWorld == nullptr)
	{
		UE_LOG(LGUI, Error, TEXT("[LGUIBuilder] Build called with null world."));
		return Result;
	}
	Result.Root = BuildInternal(InWorld, InParent, Result);
	// the root's own slot configs have no parent stack frame to apply them -- when the
	// build target is a child of an existing panel layout, honor them here
	if (Result.Root != nullptr && SlotConfigs.Num() > 0)
	{
		UUIPanelLayoutBase* ParentLayout = nullptr;
		if (InParent != nullptr && InParent->GetOwner() != nullptr)
		{
			ParentLayout = InParent->GetOwner()->FindComponentByClass<UUIPanelLayoutBase>();
		}
		auto RootItem = Local::GetRootUIItem(Result.Root);
		UUIPanelLayoutSlotBase* Slot = (ParentLayout != nullptr && RootItem != nullptr)
			? ParentLayout->GetOrCreateChildSlot(RootItem) : nullptr;
		if (Slot != nullptr)
		{
			for (auto& SlotConfig : SlotConfigs)
			{
				SlotConfig(Slot);
			}
		}
		else
		{
			UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] Slot settings on the built root ignored: build parent has no panel layout."));
		}
	}
	return Result;
}

FBuiltUI FUINode::BuildToScreen(UWorld* InWorld, int32 InSortOrder)const
{
	FBuiltUI Result;
	if (InWorld == nullptr)
	{
		UE_LOG(LGUI, Error, TEXT("[LGUIBuilder] BuildToScreen called with null world."));
		return Result;
	}
	UUIItem* ScreenRoot = ULGUIBPLibrary::GetOrCreateScreenSpaceUIRoot(InWorld);
	if (ScreenRoot == nullptr)
	{
		UE_LOG(LGUI, Error, TEXT("[LGUIBuilder] BuildToScreen could not find or create a screen-space UI root."));
		return Result;
	}
	Result = Build(InWorld, ScreenRoot);
	// UMG AddToViewport semantics: fill the screen unless geometry was chosen explicitly
	// (Size / Anchor / AnchoredPosition / FillParent on the root node)
	if (Result.Root != nullptr && !bHasExplicitGeometry)
	{
		if (auto RootItem = Local::GetRootUIItem(Result.Root))
		{
			Local::StretchToFillParent(RootItem);
		}
	}
	if (Result.Root != nullptr && InSortOrder != 0)
	{
		// own canvas layer, AddToViewport(ZOrder) style
		auto Canvas = Result.Root->FindComponentByClass<ULGUICanvas>();
		if (Canvas == nullptr)
		{
			Canvas = NewObject<ULGUICanvas>(Result.Root, NAME_None, RF_Transactional);
			Result.Root->AddInstanceComponent(Canvas);
			Canvas->RegisterComponent();
		}
		Canvas->SetSortOrder(InSortOrder, true);
	}
	return Result;
}

FBuiltUI FUINode::BuildToScreen(UWorld* InWorld, FName InScreenName, int32 InSortOrder)const
{
	FBuiltUI Result = BuildToScreen(InWorld, InSortOrder);
	if (Result.Root != nullptr && !InScreenName.IsNone())
	{
		if (auto ScreenUISubsystem = ULGUIScreenUISubsystem::Get(InWorld))
		{
			// sort order was already applied above; register with 0 so it is not re-applied
			ScreenUISubsystem->RegisterUI(InScreenName, Result.Root, 0);
		}
	}
	return Result;
}

AActor* FUINode::BuildInternal(UWorld* InWorld, USceneComponent* InParent, FBuiltUI& OutResult)const
{
	AActor* Actor = nullptr;
	if (Prefab != nullptr)
	{
		Actor = Prefab->LoadPrefabWithTransform(InWorld, InParent
			, FVector::ZeroVector, FQuat::Identity, FVector::OneVector, nullptr);
	}
	else if (ActorClass != nullptr)
	{
		Actor = InWorld->SpawnActor<AActor>(ActorClass);
		if (Actor != nullptr)
		{
			if (auto RootComp = Actor->GetRootComponent())
			{
				if (InParent != nullptr)
				{
					RootComp->AttachToComponent(InParent, FAttachmentTransformRules::KeepRelativeTransform);
				}
				RootComp->SetRelativeTransform(FTransform::Identity);
			}
			else
			{
				// sanctioned teardown for a spawned actor (unregisters from world/level);
				// ConditionalBeginDestroy would leave a dying object in the level's actor list
				InWorld->DestroyActor(Actor);
				Actor = nullptr;
			}
		}
	}
	if (Actor == nullptr)
	{
		UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] Node produced no actor (empty class/prefab?), skipped with its children."));
		return nullptr;
	}

	// extra components (panel layouts, behaviours, ...) must exist before configs run.
	// NAME_None keeps naming runtime-safe (this is a runtime module; the editor-only
	// pretty-name helper lives in UnrealEd). Each entry's config gets ITS instance --
	// find-by-class would alias onto pre-existing components of the same class.
	for (auto& Extra : ExtraComponents)
	{
		if (Extra.Class == nullptr)continue;
		auto Component = NewObject<UActorComponent>(Actor, Extra.Class, NAME_None, RF_Transactional);
		Actor->AddInstanceComponent(Component);
		Component->RegisterComponent();
		if (Extra.Config)
		{
			Extra.Config(Component);
		}
	}
	for (auto& Config : ActorConfigs)
	{
		Config(Actor);
	}
	if (!ElementName.IsEmpty())
	{
		if (AActor** Existing = OutResult.NamedElements.Find(ElementName))
		{
			UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] Duplicate element name \"%s\": \"%s\" replaces \"%s\" in the lookup."),
				*ElementName, *Actor->GetName(), *(*Existing)->GetName());
		}
		OutResult.NamedElements.Add(ElementName, Actor);
#if WITH_EDITOR
		if (!InWorld->IsGameWorld())
		{
			Actor->SetActorLabel(ElementName);
		}
#endif
	}

	// children attach under this element's UIItem; their slot configs need this
	// element's panel layout (slots are created on demand via GetOrCreateChildSlot)
	UUIItem* SelfItem = Local::GetRootUIItem(Actor);
	USceneComponent* ChildParent = SelfItem != nullptr ? SelfItem : Actor->GetRootComponent();
	UUIPanelLayoutBase* PanelLayout = Actor->FindComponentByClass<UUIPanelLayoutBase>();
	for (auto& Child : Children)
	{
		AActor* ChildActor = Child.BuildInternal(InWorld, ChildParent, OutResult);
		if (ChildActor == nullptr)continue;
		if (Child.SlotConfigs.Num() == 0)continue;
		if (PanelLayout == nullptr)
		{
			UE_LOG(LGUI, Warning, TEXT("[LGUIBuilder] Slot settings on \"%s\" ignored: parent \"%s\" has no panel layout."), *ChildActor->GetName(), *Actor->GetName());
			continue;
		}
		if (auto ChildItem = Local::GetRootUIItem(ChildActor))
		{
			if (auto Slot = PanelLayout->GetOrCreateChildSlot(ChildItem))
			{
				for (auto& SlotConfig : Child.SlotConfigs)
				{
					SlotConfig(Slot);
				}
			}
		}
	}
	return Actor;
}

//--- factories ---

FUINode Element(TSubclassOf<AActor> InActorClass)
{
	FUINode Node;
	Node.ActorClass = InActorClass;
	return Node;
}
FUINode Container()
{
	return Element(AUIContainerActor::StaticClass());
}
FUINode Image(ULGUISpriteData_BaseObject* InSprite)
{
	FUINode Node = Element(AUISpriteActor::StaticClass());
	if (InSprite != nullptr)
	{
		Node.Sprite(InSprite);
	}
	return Node;
}
FUINode TextBlock(const FText& InText)
{
	FUINode Node = Element(AUITextActor::StaticClass());
	Node.Text(InText);
	return Node;
}
FUINode TextureImage(UTexture* InTexture)
{
	FUINode Node = Element(AUITextureActor::StaticClass());
	if (InTexture != nullptr)
	{
		Node.Texture(InTexture);
	}
	return Node;
}
FUINode FromPrefab(ULGUIPrefab* InPrefab)
{
	FUINode Node;
	Node.Prefab = InPrefab;
	return Node;
}
FUINode Button()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ UUIButtonComponent::StaticClass(), nullptr });
	Node.RaycastTarget(true);
	return Node;
}
FUINode HorizontalBox()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ UUIPanelLayout_HorizontalBox::StaticClass(), nullptr });
	return Node;
}
FUINode VerticalBox()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ UUIPanelLayout_VerticalBox::StaticClass(), nullptr });
	return Node;
}
FUINode Overlay()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ UUIPanelLayout_Overlay::StaticClass(), nullptr });
	return Node;
}
FUINode UniformGrid()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ UUIPanelLayout_UniformGrid::StaticClass(), nullptr });
	return Node;
}
FUINode Canvas()
{
	FUINode Node = Container();
	Node.ExtraComponents.Add({ ULGUICanvas::StaticClass(), nullptr });
	return Node;
}

}//namespace LGUIBuilder
