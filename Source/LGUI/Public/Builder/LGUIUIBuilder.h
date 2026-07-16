// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "Types/SlateEnums.h"
#include "Layout/Margin.h"
#include "GameFramework/Actor.h"//complete type: FBuiltUI::GetComponent<T> instantiates in user TUs
#include <initializer_list>

class UActorComponent;
class USceneComponent;
class UWorld;
class UUIItem;
class UUIPanelLayoutSlotBase;
class ULGUIPrefab;
class ULGUISpriteData_BaseObject;
class ULGUIFontData_BaseObject;
class UTexture;

/**
 * Declarative, Slate-style UI building for LGUI: describe the element tree in code,
 * then Build() it into real actors. The code's indentation IS the UI's hierarchy.
 *
 *   using namespace LGUIBuilder;
 *   auto UI = VerticalBox().Padding(16)
 *   [{
 *       TextBlock(LOCTEXT("Title", "Settings")).Name(TEXT("Title")).FontSize(32),
 *       HorizontalBox()
 *       [{
 *           TextBlock(LOCTEXT("Volume", "Volume")).SizeAuto().DesiredSizeAuto(),
 *           FromPrefab(SliderPrefab).Name(TEXT("VolumeSlider")).SizeFill()
 *       }],
 *       Button().Name(TEXT("ConfirmBtn")).Size(200, 60)
 *           .OnClick(this, [this]{ OnConfirm(); })   // owner-aware: auto-disarms when 'this' dies
 *       [
 *           TextBlock(LOCTEXT("Confirm", "OK"))
 *       ]
 *   }]
 *   .BuildToScreen(GetWorld());   // AddToViewport-style; or .Build(World, ParentUIItem)
 *
 *   UI.GetComponent<UUIText>(TEXT("Title"))->SetText(...);   // named lookup
 *
 * Design notes:
 * - FUINode is a plain value-semantics DESCRIPTION; Build() is the only side effect.
 *   The data members are public on purpose: an alternative frontend (blueprint library,
 *   AngelScript wrapper) can fill the same description without the C++ sugar, and a
 *   future editor tool can walk a node tree to emit a prefab asset instead of live actors.
 * - Build once, then update imperatively via the named references (or the prefab
 *   companion-behaviour bindings). This is NOT an immediate-mode / reconciling UI.
 * - Lifetime contract: describe and Build in the same scope. The description holds RAW
 *   UObject pointers (prefab, sprite, texture, blueprint classes) with no GC visibility;
 *   a node tree kept across frames needs those assets rooted elsewhere. Always store a
 *   chain result BY VALUE (auto Tree = ...), never by reference -- the fluent chain runs
 *   on a temporary and 'const auto&' would dangle.
 * - A node is either an actor class (ActorClass) or a prefab instance (Prefab).
 *   Panel factories (HorizontalBox/VerticalBox/Overlay/UniformGrid) are a container
 *   actor plus the matching PanelLayout component, same recipe as the palette templates.
 * - Slot methods (Padding/HAlign/SizeFill/Cell/...) configure THIS element's slot in its
 *   parent panel; they silently no-op when the parent has no such slot type.
 * - Type-specific sugar (Text/FontSize/Sprite/...) applies to the element's root
 *   component and no-ops with a log warning when the type does not match.
 */
namespace LGUIBuilder
{
	/** Build() result: the root actor plus every .Name()-ed element for immediate lookup. */
	struct LGUI_API FBuiltUI
	{
		AActor* Root = nullptr;
		/** Elements registered via .Name(). Raw pointers: consume right after Build, or store weak. */
		TMap<FString, AActor*> NamedElements;

		AActor* GetActor(const FString& InName)const
		{
			if (auto Found = NamedElements.Find(InName))return *Found;
			return nullptr;
		}
		template<class T>
		T* GetComponent(const FString& InName)const
		{
			if (AActor* Actor = GetActor(InName))
			{
				return Actor->FindComponentByClass<T>();
			}
			return nullptr;
		}
	};

	struct LGUI_API FUINode
	{
		/** Extra component to create on the built actor, with an optional config applied to THAT instance. */
		struct FExtraComponent
		{
			TSubclassOf<UActorComponent> Class;
			TFunction<void(UActorComponent*)> Config;
		};

		//--- description data (public: alternative frontends fill these directly) ---
		TSubclassOf<AActor> ActorClass;
		ULGUIPrefab* Prefab = nullptr;
		FString ElementName;
		TArray<FExtraComponent> ExtraComponents;
		TArray<TFunction<void(AActor*)>> ActorConfigs;
		TArray<TFunction<void(UUIPanelLayoutSlotBase*)>> SlotConfigs;
		TArray<FUINode> Children;

		//--- hierarchy ---
		FUINode& operator[](const FUINode& InChild) { Children.Add(InChild); return *this; }
		FUINode& operator[](std::initializer_list<FUINode> InChildren)
		{
			for (auto& Child : InChildren) { Children.Add(Child); }
			return *this;
		}

		//--- common config ---
		/**
		 * Register this element in FBuiltUI::NamedElements for lookup after Build. In editor
		 * (non-game) worlds the actor is also labeled with this name; a future code-to-prefab
		 * export would carry the label into companion-behaviour auto-bind, but live-built UI
		 * is not a prefab -- bind via the FBuiltUI lookup instead.
		 */
		FUINode& Name(const FString& InName);
		FUINode& Size(float InWidth, float InHeight);
		FUINode& AnchoredPosition(float InX, float InY);
		FUINode& Anchor(const FVector2D& InMin, const FVector2D& InMax);
		FUINode& Pivot(const FVector2D& InPivot);
		/** Tint of the root renderable (sprite/text/texture). */
		FUINode& Color(const FColor& InColor);
		FUINode& RaycastTarget(bool bValue = true);
		/** Escape hatch: arbitrary configuration on the built actor. */
		FUINode& Setup(TFunction<void(AActor*)> InFunc);
		/** Attach an extra component (behaviour, effect, ...) with optional configuration. */
		FUINode& WithComponent(TSubclassOf<UActorComponent> InClass, TFunction<void(UActorComponent*)> InConfig = nullptr);

		//--- type-specific sugar (no-op with a warning when the root component type differs) ---
		FUINode& Text(const FText& InText);
		FUINode& FontSize(float InSize);
		/** Explicit font; without it texts use the project default (LGUI settings "DefaultFont", falling back to the plugin's built-in font which has NO CJK glyphs). */
		FUINode& Font(ULGUIFontData_BaseObject* InFont);
		FUINode& Sprite(ULGUISpriteData_BaseObject* InSprite, bool bSetNativeSize = false);
		FUINode& Texture(UTexture* InTexture);

		//--- slot config (this element's slot in its parent panel layout) ---
		FUINode& Padding(const FMargin& InPadding);
		FUINode& Padding(float InUniformPadding) { return Padding(FMargin(InUniformPadding)); }
		FUINode& HAlign(EHorizontalAlignment InAlign);
		FUINode& VAlign(EVerticalAlignment InAlign);
		/** HorizontalBox/VerticalBox: take a share of the leftover space (UMG SizeRule=Fill). */
		FUINode& SizeFill(float InRatio = 1.0f);
		/** HorizontalBox/VerticalBox: size by the slot's desired size (UMG SizeRule=Auto). */
		FUINode& SizeAuto();
		/** Slot manual desired size (switches the slot to Manual mode). */
		FUINode& DesiredSize(float InWidth, float InHeight);
		/** Slot measures its content (text/sprite/texture) every rebuild, UMG-style. */
		FUINode& DesiredSizeAuto();
		/** UniformGrid: which cell this element occupies. */
		FUINode& Cell(int32 InRow, int32 InColumn);
		FUINode& IgnoreLayout(bool bIgnore = true);

		//--- events ---
		/**
		 * Register on the element's UIButtonComponent (present on Button() nodes and button
		 * prefabs), tied to InOwner: the callback auto-disarms when InOwner is destroyed.
		 * Prefer this overload whenever the lambda captures a UObject.
		 */
		FUINode& OnClick(UObject* InOwner, TFunction<void()> InCallback);
		/** Ownerless variant: the captured objects MUST outlive the built UI. */
		FUINode& OnClick(TFunction<void()> InCallback);

		//--- build ---
		FBuiltUI Build(UWorld* InWorld, USceneComponent* InParent)const;
		/**
		 * UMG AddToViewport counterpart, for screen-space UI: build under the world's
		 * screen-space UI root, which is found -- or created from the plugin's Basic-Setup
		 * prefab (canvas + scaler + event system) -- on demand.
		 * @param InSortOrder When not 0, the built root gets its own canvas layer with this
		 *                    sort order (AddToViewport's ZOrder).
		 */
		FBuiltUI BuildToScreen(UWorld* InWorld, int32 InSortOrder = 0)const;
		/**
		 * BuildToScreen + register the result in ULGUIScreenUISubsystem under InScreenName,
		 * so the page is reachable by name from anywhere (GetUI / SetUIVisible / RemoveUI).
		 */
		FBuiltUI BuildToScreen(UWorld* InWorld, FName InScreenName, int32 InSortOrder)const;

	private:
		AActor* BuildInternal(UWorld* InWorld, USceneComponent* InParent, FBuiltUI& OutResult)const;
	};

	//--- element factories ---
	/** Any UI actor class. */
	LGUI_API FUINode Element(TSubclassOf<AActor> InActorClass);
	/** Plain UI container. */
	LGUI_API FUINode Container();
	/** Sprite element (UMG: Image). */
	LGUI_API FUINode Image(ULGUISpriteData_BaseObject* InSprite = nullptr);
	/** Text element (UMG: Text Block). */
	LGUI_API FUINode TextBlock(const FText& InText);
	/** Raw texture element. */
	LGUI_API FUINode TextureImage(UTexture* InTexture = nullptr);
	/** Instance of a prefab asset (buttons/sliders/your own composites). */
	LGUI_API FUINode FromPrefab(ULGUIPrefab* InPrefab);
	/**
	 * Clickable container: hit area = its rect (RaycastTarget on), visuals = your children.
	 * For a fully styled button prefer FromPrefab with the built-in Button prefab.
	 */
	LGUI_API FUINode Button();

	//--- panel factories (container + panel layout, same recipe as the palette templates) ---
	LGUI_API FUINode HorizontalBox();
	LGUI_API FUINode VerticalBox();
	LGUI_API FUINode Overlay();
	LGUI_API FUINode UniformGrid();
	/**
	 * Container + LGUICanvas. LGUI only renders/raycasts elements under a canvas, so a
	 * screen built with a null parent needs one of these at (or near) its root.
	 */
	LGUI_API FUINode Canvas();
}
