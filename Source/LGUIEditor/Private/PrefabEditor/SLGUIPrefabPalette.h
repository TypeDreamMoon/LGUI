// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Misc/TextFilterExpressionEvaluator.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include <atomic>

class FLGUIPrefabEditor;
class FAssetThumbnailPool;
class ULGUIPrefab;

/** Palette tab groups, UMG-style: element types, behaviour components, prefab assets. */
enum class ELGUIPaletteTab : uint8
{
	Elements = 0,
	Components,
	Prefabs,
};

/**
 * A composed element, UMG-Palette-style: an actor class plus the components that make it
 * that element (e.g. "Horizontal Box" = UIContainerActor + UIPanelLayout_HorizontalBox).
 * Saves the two-step "drag container, then drag layout component" dance.
 */
struct FLGUIPaletteElementTemplate
{
	FText DisplayName;
	FText Tooltip;
	TSubclassOf<AActor> ActorClass;
	TArray<TSubclassOf<UActorComponent>> ComponentClasses;

	/** The actor class + component classes as one asset-data list, the drag/drop payload format. */
	TArray<FAssetData> BuildAssetList()const
	{
		TArray<FAssetData> Assets;
		Assets.Add(FAssetData(ActorClass.Get()));
		for (auto& ComponentClass : ComponentClasses)
		{
			Assets.Add(FAssetData(ComponentClass.Get()));
		}
		return Assets;
	}
};

/**
 * Drag payload for element templates. Subclasses FAssetDragDropOp so the whole existing
 * asset drop pipeline (viewport drop, outliner ParseDragDrop/ValidateDrop/OnDrop) accepts
 * it unchanged via IsOfType<FAssetDragDropOp>(); drop sites that additionally check for
 * THIS type can read the template name to label the created actor.
 */
class FLGUIElementTemplateDragDropOp : public FAssetDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FLGUIElementTemplateDragDropOp, FAssetDragDropOp)

	FText TemplateDisplayName;

	static TSharedRef<FLGUIElementTemplateDragDropOp> New(const FLGUIPaletteElementTemplate& InTemplate)
	{
		TSharedRef<FLGUIElementTemplateDragDropOp> Operation = MakeShared<FLGUIElementTemplateDragDropOp>();
		Operation->TemplateDisplayName = InTemplate.DisplayName;
		Operation->Init(InTemplate.BuildAssetList(), TArray<FString>(), nullptr);
		// show the template name while dragging, not the raw class list
		Operation->CurrentHoverText = Operation->DefaultHoverText = InTemplate.DisplayName;
		Operation->Construct();
		return Operation;
	}
};

/**
 * One node of the palette tree: a category header (CategoryName set), a prefab asset row
 * (Asset valid), a class row (ComponentClass valid -- either a behaviour/effect component
 * class to add to an actor, or a UI element ACTOR class to spawn under it), or an element
 * template row (Template valid).
 */
struct FLGUIPrefabPaletteItem
{
	FAssetData Asset;
	/** Component class (added to the target actor) OR UI element actor class (spawned under it). */
	TWeakObjectPtr<UClass> ComponentClass;
	/** Composed element: actor class + components, spawned as one unit. */
	TSharedPtr<FLGUIPaletteElementTemplate> Template;
	FString CategoryName;
	/** True on the pinned "Favorites" header (distinguishes it from a user category literally named "Favorites"). */
	bool bIsFavoritesGroup = false;
	/** True on the fixed component-class group headers (Interaction/Layout/Effect/Behaviour). */
	bool bIsComponentGroup = false;
	TArray<TSharedPtr<FLGUIPrefabPaletteItem>> Children;

	bool IsComponentClass()const { return ComponentClass.IsValid(); }
	bool IsElementTemplate()const { return Template.IsValid(); }
	bool IsCategory()const { return !Asset.IsValid() && !ComponentClass.IsValid() && !Template.IsValid(); }
};

/**
 * "Prefab Palette" tab content for the Prefab Editor, modeled on UMG's Palette:
 * lists every ULGUIPrefab asset in the project grouped by its PaletteCategory,
 * with a search box and thumbnails. Rows are dragged into the viewport as a
 * standard FAssetDragDropOp, which the existing viewport drop path already
 * handles (attach under the selected actor as a sub prefab).
 */
class SLGUIPrefabPalette : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLGUIPrefabPalette) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor);
	virtual ~SLGUIPrefabPalette();

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	typedef TSharedPtr<FLGUIPrefabPaletteItem> FItemPtr;

	// tree + data
	void RebuildList();
	FString GetCategoryForAsset(const FAssetData& InAssetData)const;
	TSharedRef<ITableRow> OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren);
	void OnItemDoubleClick(FItemPtr InItem);
	TSharedPtr<SWidget> OnContextMenuOpening();

	// search
	void OnSearchTextChanged(const FText& InText);

	// drag
	FReply OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem);

	// context menu actions
	void BrowseToAsset(FItemPtr InItem);
	void SetItemCategory(FItemPtr InItem, FString NewCategory);
	void OnNewCategoryTextCommitted(const FText& InText, ETextCommit::Type CommitType, FItemPtr InItem);

	// favorites (persisted per project in the editor ini)
	bool IsFavorite(const FAssetData& InAssetData)const;
	void ToggleFavorite(FItemPtr InItem);
	void LoadFavorites();
	void SaveFavorites()const;

	// hide-in-palette (stored on the prefab asset, ULGUIPrefab::bHideInPalette)
	bool IsAssetHiddenInPalette(const FAssetData& InAssetData)const;
	void ToggleItemHidden(FItemPtr InItem);

	// component class rows
	void CollectComponentClassGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader);
	void AddComponentToSelectedActor(FItemPtr InItem);
	/** Spawn an element template (actor + components) under the actor selected in the outliner. */
	void AddTemplateUnderSelectedActor(FItemPtr InItem);
	/** Favorites key: object path for assets, class path for component classes, name for templates. */
	FString GetItemFavoriteKey(FItemPtr InItem)const;

	// tab groups
	void CollectElementGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader);
	void CollectPrefabCategories(TArray<FItemPtr>& OutHeaders, FItemPtr& InOutFavoritesHeader);
	void AddToFavoritesHeaderIfFavorite(const FItemPtr& InItem, FItemPtr& InOutFavoritesHeader);
	void SetCurrentTab(ELGUIPaletteTab InTab);

	// asset registry change handlers (may fire off the game thread -- only set the dirty flag)
	void OnAssetChanged(const FAssetData& InAssetData);
	void OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath);
	void OnAssetRegistryFilesLoaded();
	void RequestRebuild() { bPendingRebuild = true; }

private:
	TWeakPtr<FLGUIPrefabEditor> PrefabEditorPtr;

	TSharedPtr<STreeView<FItemPtr>> TreeView;
	TArray<FItemPtr> RootItems;
	/** All category names currently present (unfiltered), for the "Set Category" context submenu. */
	TArray<FString> KnownCategories;

	TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
	FTextFilterExpressionEvaluator SearchFilter{ ETextFilterExpressionEvaluatorMode::BasicString };
	/** Object paths of favorited prefab assets. */
	TSet<FString> FavoritePaths;
	/** When true, prefabs marked bHideInPalette are shown (greyed out) so they can be unhidden. */
	bool bShowHiddenPrefabs = false;
	/** Active tab group (persisted in the editor ini). */
	ELGUIPaletteTab CurrentTab = ELGUIPaletteTab::Elements;

	std::atomic<bool> bPendingRebuild{ false };

	FDelegateHandle OnAssetAddedHandle;
	FDelegateHandle OnAssetRemovedHandle;
	FDelegateHandle OnAssetRenamedHandle;
	FDelegateHandle OnAssetUpdatedHandle;
	FDelegateHandle OnFilesLoadedHandle;
};
