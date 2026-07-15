// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "SLGUIPrefabPalette.h"
#include "LGUIPrefabEditor.h"
#include "LGUIEditorTools.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "Core/LGUILifeCycleBehaviour.h"
#include "GeometryModifier/UIGeometryModifierBase.h"
#include "Interaction/UISelectableComponent.h"
#include "Layout/UILayoutBase.h"
#include "Layout/UILayoutElement.h"
#include "Core/Actor/UIContainerActor.h"
#include "Core/Actor/UISpriteActor.h"
#include "Core/Actor/UITextActor.h"
#include "Core/Actor/UITextureActor.h"
#include "Core/Actor/UIProceduralRectActor.h"
#include "Core/Actor/UICustomMeshActor.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetThumbnail.h"
#include "ContentBrowserModule.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "UObject/UObjectHash.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabPalette"

namespace LGUIPrefabPaletteLocal
{
	const FName PaletteCategoryTagName(TEXT("PaletteCategory"));
	const TCHAR* UncategorizedKey = TEXT("");//internal key for prefabs with no category
	const TCHAR* FavoritesConfigSection = TEXT("LGUIPrefabPalette");
	const TCHAR* FavoritesConfigKey = TEXT("Favorites");

	FText GetCategoryDisplayText(const FString& InCategory)
	{
		return InCategory.IsEmpty() ? LOCTEXT("Uncategorized", "Uncategorized") : FText::FromString(InCategory);
	}
}

void SLGUIPrefabPalette::Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
	ThumbnailPool = MakeShared<FAssetThumbnailPool>(64);//FTickableEditorObject, ticks itself
	LoadFavorites();
	{
		int32 SavedTab = (int32)ELGUIPaletteTab::Elements;
		GConfig->GetInt(LGUIPrefabPaletteLocal::FavoritesConfigSection, TEXT("CurrentTab"), SavedTab, GEditorPerProjectIni);
		CurrentTab = (ELGUIPaletteTab)FMath::Clamp(SavedTab, 0, (int32)ELGUIPaletteTab::Prefabs);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 2, 2, 2)
		[
			// tab groups, UMG-style
			SNew(SSegmentedControl<ELGUIPaletteTab>)
			.Value_Lambda([this]() { return CurrentTab; })
			.OnValueChanged(this, &SLGUIPrefabPalette::SetCurrentTab)
			+ SSegmentedControl<ELGUIPaletteTab>::Slot(ELGUIPaletteTab::Elements)
			.Text(LOCTEXT("TabElements", "Elements"))
			.ToolTip(LOCTEXT("TabElementsTooltip", "Basic UI element types and built-in controls (same as the Create UI Element menu)"))
			+ SSegmentedControl<ELGUIPaletteTab>::Slot(ELGUIPaletteTab::Components)
			.Text(LOCTEXT("TabComponents", "Components"))
			.ToolTip(LOCTEXT("TabComponentsTooltip", "LGUI behaviour components: interaction, layout, effects and custom scripts"))
			+ SSegmentedControl<ELGUIPaletteTab>::Slot(ELGUIPaletteTab::Prefabs)
			.Text(LOCTEXT("TabPrefabs", "Prefabs"))
			.ToolTip(LOCTEXT("TabPrefabsTooltip", "Prefab assets in this project, grouped by palette category"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 2, 2, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Search prefabs"))
				.OnTextChanged(this, &SLGUIPrefabPalette::OnSearchTextChanged)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.Style(FAppStyle::Get(), "ToggleButtonCheckbox")
				.ToolTipText(LOCTEXT("ShowHiddenTooltip", "Show prefabs marked \"Hide In Palette\" (greyed out) so they can be unhidden"))
				.IsChecked_Lambda([this]() { return bShowHiddenPrefabs ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState NewState)
					{
						bShowHiddenPrefabs = (NewState == ECheckBoxState::Checked);
						RequestRebuild();
					})
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Visibility"))
					.DesiredSizeOverride(FVector2D(16, 16))
				]
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TreeView, STreeView<FItemPtr>)
			.SelectionMode(ESelectionMode::Single)
			.TreeItemsSource(&RootItems)
			.OnGenerateRow(this, &SLGUIPrefabPalette::OnGenerateRow)
			.OnGetChildren(this, &SLGUIPrefabPalette::OnGetChildren)
			.OnMouseButtonDoubleClick(this, &SLGUIPrefabPalette::OnItemDoubleClick)
			.OnContextMenuOpening(this, &SLGUIPrefabPalette::OnContextMenuOpening)
		]
	];

	// keep the list in sync with the project's assets; handlers may fire off the game
	// thread during scans, so they only set a flag consumed in Tick
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	OnAssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnAssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddSP(this, &SLGUIPrefabPalette::OnAssetRenamed);
	OnAssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddSP(this, &SLGUIPrefabPalette::OnAssetChanged);
	OnFilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddSP(this, &SLGUIPrefabPalette::OnAssetRegistryFilesLoaded);

	RebuildList();
}

SLGUIPrefabPalette::~SLGUIPrefabPalette()
{
	// registry may already be torn down at editor shutdown
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetAdded().Remove(OnAssetAddedHandle);
		AssetRegistry->OnAssetRemoved().Remove(OnAssetRemovedHandle);
		AssetRegistry->OnAssetRenamed().Remove(OnAssetRenamedHandle);
		AssetRegistry->OnAssetUpdated().Remove(OnAssetUpdatedHandle);
		AssetRegistry->OnFilesLoaded().Remove(OnFilesLoadedHandle);
	}
}

void SLGUIPrefabPalette::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bPendingRebuild.exchange(false))
	{
		RebuildList();
	}
}

FString SLGUIPrefabPalette::GetCategoryForAsset(const FAssetData& InAssetData)const
{
	// loaded assets may have an unsaved category edit -- prefer the live property
	// (IsAssetLoaded guarantees GetAsset() won't trigger a load)
	if (InAssetData.IsAssetLoaded())
	{
		if (auto Prefab = Cast<ULGUIPrefab>(InAssetData.GetAsset()))
		{
#if WITH_EDITORONLY_DATA
			return Prefab->PaletteCategory;
#endif
		}
	}
	return InAssetData.GetTagValueRef<FString>(LGUIPrefabPaletteLocal::PaletteCategoryTagName);
}

void SLGUIPrefabPalette::CollectComponentClassGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader)
{
	// LGUI behaviour components (interaction, layout, custom scripts) plus geometry-modifier
	// effects (UIEffect*), whose base derives straight from UActorComponent
	TArray<UClass*> CandidateClasses;
	GetDerivedClasses(ULGUILifeCycleBehaviour::StaticClass(), CandidateClasses, true);
	{
		TArray<UClass*> EffectClasses;
		GetDerivedClasses(UUIGeometryModifierBase::StaticClass(), EffectClasses, true);
		for (auto& EffectClass : EffectClasses)
		{
			CandidateClasses.AddUnique(EffectClass);
		}
	}

	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	// fixed group order: most used first (shown inside the Components tab, so no prefix needed)
	const FString GroupInteraction = TEXT("Interaction");
	const FString GroupLayout = TEXT("Layout");
	const FString GroupEffect = TEXT("Effect");
	const FString GroupBehaviour = TEXT("Behaviour");
	TMap<FString, FItemPtr> GroupMap;
	auto GetGroupHeader = [&GroupMap](const FString& GroupName) -> FItemPtr&
		{
			FItemPtr& Header = GroupMap.FindOrAdd(GroupName);
			if (!Header.IsValid())
			{
				Header = MakeShared<FLGUIPrefabPaletteItem>();
				Header->CategoryName = GroupName;
				Header->bIsComponentGroup = true;
			}
			return Header;
		};

	for (auto& Class : CandidateClasses)
	{
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_HideDropDown | CLASS_NewerVersionExists))
		{
			continue;
		}
		// skip blueprint skeleton/reinstanced classes
		const FString ClassName = Class->GetName();
		if (ClassName.StartsWith(TEXT("SKEL_")) || ClassName.StartsWith(TEXT("REINST_")))
		{
			continue;
		}

		const FString DisplayName = Class->GetDisplayNameText().ToString();
		if (bFilterActive
			&& !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(DisplayName))
			&& !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(ClassName)))
		{
			continue;
		}

		FString GroupName;
		if (Class->IsChildOf(UUISelectableComponent::StaticClass()))
		{
			GroupName = GroupInteraction;
		}
		else if (Class->IsChildOf(UUILayoutBase::StaticClass()) || Class->IsChildOf(UUILayoutElement::StaticClass()))
		{
			GroupName = GroupLayout;
		}
		else if (Class->IsChildOf(UUIGeometryModifierBase::StaticClass()))
		{
			GroupName = GroupEffect;
		}
		else
		{
			GroupName = GroupBehaviour;
		}

		auto Item = MakeShared<FLGUIPrefabPaletteItem>();
		Item->ComponentClass = Class;
		GetGroupHeader(GroupName)->Children.Add(Item);

		if (FavoritePaths.Contains(Class->GetClassPathName().ToString()))
		{
			if (!InOutFavoritesHeader.IsValid())
			{
				InOutFavoritesHeader = MakeShared<FLGUIPrefabPaletteItem>();
				InOutFavoritesHeader->bIsFavoritesGroup = true;
			}
			auto FavItem = MakeShared<FLGUIPrefabPaletteItem>();
			FavItem->ComponentClass = Class;
			InOutFavoritesHeader->Children.Add(FavItem);
		}
	}

	// emit in fixed order, sort children by display name
	for (const FString& GroupName : { GroupInteraction, GroupLayout, GroupEffect, GroupBehaviour })
	{
		if (FItemPtr* Header = GroupMap.Find(GroupName))
		{
			(*Header)->Children.Sort([](const FItemPtr& A, const FItemPtr& B) {
				return A->ComponentClass->GetDisplayNameText().CompareTo(B->ComponentClass->GetDisplayNameText()) < 0;
				});
			OutGroupHeaders.Add(*Header);
		}
	}
}

void SLGUIPrefabPalette::AddToFavoritesHeaderIfFavorite(const FItemPtr& InItem, FItemPtr& InOutFavoritesHeader)
{
	if (!FavoritePaths.Contains(GetItemFavoriteKey(InItem)))return;
	if (!InOutFavoritesHeader.IsValid())
	{
		InOutFavoritesHeader = MakeShared<FLGUIPrefabPaletteItem>();
		InOutFavoritesHeader->bIsFavoritesGroup = true;
	}
	auto FavItem = MakeShared<FLGUIPrefabPaletteItem>();
	FavItem->Asset = InItem->Asset;
	FavItem->ComponentClass = InItem->ComponentClass;
	InOutFavoritesHeader->Children.Add(FavItem);
}

void SLGUIPrefabPalette::SetCurrentTab(ELGUIPaletteTab InTab)
{
	if (CurrentTab == InTab)return;
	CurrentTab = InTab;
	GConfig->SetInt(LGUIPrefabPaletteLocal::FavoritesConfigSection, TEXT("CurrentTab"), (int32)CurrentTab, GEditorPerProjectIni);
	RequestRebuild();
}

void SLGUIPrefabPalette::CollectElementGroups(TArray<FItemPtr>& OutGroupHeaders, FItemPtr& InOutFavoritesHeader)
{
	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();

	// basic UI element actor classes -- same list as the "Create UI Element" menu. Dragging them
	// spawns the actor under the drop target (the drop path already handles actor classes).
	{
		UClass* BasicClasses[] =
		{
			AUIContainerActor::StaticClass(),
			AUISpriteActor::StaticClass(),
			AUITextActor::StaticClass(),
			AUITextureActor::StaticClass(),
			AUIProceduralRectActor::StaticClass(),
			AUICustomMeshActor::StaticClass(),
		};
		FItemPtr Header;
		for (UClass* Class : BasicClasses)
		{
			FString ShortName = Class->GetName();
			ShortName.RemoveFromEnd(TEXT("Actor"));
			if (bFilterActive && !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(ShortName)))
			{
				continue;
			}
			if (!Header.IsValid())
			{
				Header = MakeShared<FLGUIPrefabPaletteItem>();
				Header->CategoryName = TEXT("Basic");
				Header->bIsComponentGroup = true;
			}
			auto Item = MakeShared<FLGUIPrefabPaletteItem>();
			Item->ComponentClass = Class;
			Header->Children.Add(Item);
			AddToFavoritesHeaderIfFavorite(Item, InOutFavoritesHeader);
		}
		if (Header.IsValid())
		{
			OutGroupHeaders.Add(Header);
		}
	}

	// built-in control prefabs (Button/Toggle/Slider/...) -- same list as the "Create UI Element"
	// menu; they are prefab assets under /LGUI/Prefabs/, so the whole prefab drag/drop path applies
	{
		static const TCHAR* ControlNames[] =
		{
			TEXT("Button"), TEXT("Toggle"), TEXT("ToggleGroup"),
			TEXT("HorizontalSlider"), TEXT("VerticalSlider"),
			TEXT("HorizontalScrollbar"), TEXT("VerticalScrollbar"),
			TEXT("Dropdown"), TEXT("TextInput"), TEXT("TextInputMultiline"),
			TEXT("HorizontalScrollView"), TEXT("VerticalScrollView"),
			TEXT("HorizontalRecyclableScrollView"), TEXT("VerticalRecyclableScrollView"),
		};
		IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		FItemPtr Header;
		for (const TCHAR* ControlName : ControlNames)
		{
			if (bFilterActive && !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(ControlName)))
			{
				continue;
			}
			const FString ObjectPath = FString::Printf(TEXT("%s%s.%s"), *LGUIEditorTools::LGUIPresetPrefabPath, ControlName, ControlName);
			FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
			if (!AssetData.IsValid())continue;//plugin content not mounted/scanned yet
			if (!Header.IsValid())
			{
				Header = MakeShared<FLGUIPrefabPaletteItem>();
				Header->CategoryName = TEXT("Controls");
				Header->bIsComponentGroup = true;
			}
			auto Item = MakeShared<FLGUIPrefabPaletteItem>();
			Item->Asset = AssetData;
			Header->Children.Add(Item);
			AddToFavoritesHeaderIfFavorite(Item, InOutFavoritesHeader);
		}
		if (Header.IsValid())
		{
			OutGroupHeaders.Add(Header);
		}
	}
}

void SLGUIPrefabPalette::CollectPrefabCategories(TArray<FItemPtr>& OutHeaders, FItemPtr& InOutFavoritesHeader)
{
	TArray<FAssetData> PrefabAssets;
	IAssetRegistry::GetChecked().GetAssetsByClass(ULGUIPrefab::StaticClass()->GetClassPathName(), PrefabAssets, true);

	FSoftObjectPath EditingPrefabPath;
	if (auto PrefabEditor = PrefabEditorPtr.Pin())
	{
		if (auto EditingPrefab = PrefabEditor->GetPrefabBeingEdited())
		{
			EditingPrefabPath = FSoftObjectPath(EditingPrefab);
		}
	}

	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	TMap<FString, FItemPtr> CategoryMap;
	for (auto& AssetData : PrefabAssets)
	{
		// the prefab being edited can never be its own child -- hide it
		if (EditingPrefabPath.IsValid() && AssetData.ToSoftObjectPath() == EditingPrefabPath)
		{
			continue;
		}
		// the plugin's built-in preset prefabs live in the Elements tab
		if (AssetData.PackageName.ToString().StartsWith(TEXT("/LGUI/Prefabs/")))
		{
			continue;
		}
		// respect the asset's "hide in palette" flag unless the show-hidden filter is on
		if (!bShowHiddenPrefabs && IsAssetHiddenInPalette(AssetData))
		{
			continue;
		}

		const FString Category = GetCategoryForAsset(AssetData);
		KnownCategories.AddUnique(Category);

		if (bFilterActive && !SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(AssetData.AssetName.ToString())))
		{
			continue;
		}

		FItemPtr& Header = CategoryMap.FindOrAdd(Category);
		if (!Header.IsValid())
		{
			Header = MakeShared<FLGUIPrefabPaletteItem>();
			Header->CategoryName = Category;
		}
		auto Item = MakeShared<FLGUIPrefabPaletteItem>();
		Item->Asset = AssetData;
		Header->Children.Add(Item);
		AddToFavoritesHeaderIfFavorite(Item, InOutFavoritesHeader);
	}

	CategoryMap.GenerateValueArray(OutHeaders);
	// alphabetical categories, "Uncategorized" (empty) pinned to the bottom
	OutHeaders.Sort([](const FItemPtr& A, const FItemPtr& B) {
		if (A->CategoryName.IsEmpty() != B->CategoryName.IsEmpty())
		{
			return B->CategoryName.IsEmpty();
		}
		return A->CategoryName < B->CategoryName;
		});
	// prefab rows in name order (class-item groups keep their own curated order)
	for (auto& Header : OutHeaders)
	{
		Header->Children.Sort([](const FItemPtr& A, const FItemPtr& B) {
			return A->Asset.AssetName.LexicalLess(B->Asset.AssetName);
			});
	}
	KnownCategories.Sort();
}

void SLGUIPrefabPalette::RebuildList()
{
	RootItems.Reset();
	KnownCategories.Reset();

	FItemPtr FavoritesHeader;
	switch (CurrentTab)
	{
	case ELGUIPaletteTab::Elements:
		CollectElementGroups(RootItems, FavoritesHeader);
		break;
	case ELGUIPaletteTab::Components:
		CollectComponentClassGroups(RootItems, FavoritesHeader);
		break;
	case ELGUIPaletteTab::Prefabs:
		CollectPrefabCategories(RootItems, FavoritesHeader);
		break;
	}

	// Favorites (of the current tab's content) pinned to the very top
	if (FavoritesHeader.IsValid())
	{
		RootItems.Insert(FavoritesHeader, 0);
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		for (auto& Header : RootItems)
		{
			TreeView->SetItemExpansion(Header, true);
		}
	}
}

TSharedRef<ITableRow> SLGUIPrefabPalette::OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InItem->IsCategory())
	{
		const FText HeaderText = InItem->bIsFavoritesGroup
			? LOCTEXT("FavoritesGroup", "Favorites")
			: LGUIPrefabPaletteLocal::GetCategoryDisplayText(InItem->CategoryName);
		return SNew(STableRow<FItemPtr>, OwnerTable)
			.ShowSelection(false)
			.Padding(FMargin(4, 3))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0, 0, 4, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Star"))
					.DesiredSizeOverride(FVector2D(12, 12))
					.Visibility(InItem->bIsFavoritesGroup ? EVisibility::Visible : EVisibility::Collapsed)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
					.Text(HeaderText.ToUpper())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			];
	}

	if (InItem->IsComponentClass())
	{
		UClass* Class = InItem->ComponentClass.Get();
		return SNew(STableRow<FItemPtr>, OwnerTable)
			.Padding(FMargin(2, 2))
			.OnDragDetected(FOnDragDetected::CreateSP(this, &SLGUIPrefabPalette::OnItemDragDetected, InItem))
			.ToolTipText(FText::Format(LOCTEXT("ComponentRowTooltip", "{0}\nDrag onto an actor (viewport or outliner row) to add this component. Double-click adds it to the selected actor.")
				, FText::FromString(Class->GetClassPathName().ToString())))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(2, 0, 6, 0)
				[
					SNew(SImage)
					.Image(FSlateIconFinder::FindIconBrushForClass(Class))
					.DesiredSizeOverride(FVector2D(16, 16))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Class->GetDisplayNameText())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 0)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.Star"))
					.DesiredSizeOverride(FVector2D(10, 10))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this, WeakItem = TWeakPtr<FLGUIPrefabPaletteItem>(InItem)]()
						{
							auto Pinned = WeakItem.Pin();
							return (Pinned.IsValid() && FavoritePaths.Contains(GetItemFavoriteKey(Pinned))) ? EVisibility::Visible : EVisibility::Collapsed;
						})
				]
			];
	}

	auto Thumbnail = MakeShared<FAssetThumbnail>(InItem->Asset, 32, 32, ThumbnailPool);
	return SNew(STableRow<FItemPtr>, OwnerTable)
		.Padding(FMargin(2, 1))
		.OnDragDetected(FOnDragDetected::CreateSP(this, &SLGUIPrefabPalette::OnItemDragDetected, InItem))
		.ToolTipText(FText::Format(LOCTEXT("PrefabRowTooltip", "{0}\nDrag into the viewport to add as sub prefab (attaches under the selected actor).")
			, FText::FromString(InItem->Asset.GetSoftObjectPath().ToString())))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0, 0, 6, 0)
			[
				SNew(SBox)
				.WidthOverride(32)
				.HeightOverride(32)
				[
					Thumbnail->MakeThumbnailWidget()
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromName(InItem->Asset.AssetName))
				// hidden prefabs (visible via the show-hidden filter) render greyed out
				.ColorAndOpacity_Lambda([this, WeakItem = TWeakPtr<FLGUIPrefabPaletteItem>(InItem)]()
					{
						auto Pinned = WeakItem.Pin();
						return (Pinned.IsValid() && IsAssetHiddenInPalette(Pinned->Asset))
							? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground();
					})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("Icons.Star"))
				.DesiredSizeOverride(FVector2D(10, 10))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([this, WeakItem = TWeakPtr<FLGUIPrefabPaletteItem>(InItem)]()
					{
						auto Pinned = WeakItem.Pin();
						return (Pinned.IsValid() && IsFavorite(Pinned->Asset)) ? EVisibility::Visible : EVisibility::Collapsed;
					})
			]
		];
}

void SLGUIPrefabPalette::OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SLGUIPrefabPalette::OnSearchTextChanged(const FText& InText)
{
	SearchFilter.SetFilterText(InText);
	RequestRebuild();
}

FReply SLGUIPrefabPalette::OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem)
{
	if (InItem.IsValid() && InItem->IsComponentClass())
	{
		// a UClass is a UObject, so it travels as a standard asset drag; the drop path
		// recognizes component classes and adds the component to the target actor
		return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(FAssetData(InItem->ComponentClass.Get())));
	}
	if (InItem.IsValid() && !InItem->IsCategory())
	{
		// standard asset drag: the viewport's existing OnDrop path (TryHandleAssetDragDropOperation)
		// consumes it exactly like a drag from the Content Browser
		return FReply::Handled().BeginDragDrop(FAssetDragDropOp::New(InItem->Asset));
	}
	return FReply::Unhandled();
}

void SLGUIPrefabPalette::OnItemDoubleClick(FItemPtr InItem)
{
	if (InItem.IsValid() && InItem->IsComponentClass())
	{
		AddComponentToSelectedActor(InItem);
	}
	else if (InItem.IsValid() && !InItem->IsCategory())
	{
		BrowseToAsset(InItem);
	}
	else if (InItem.IsValid() && TreeView.IsValid())
	{
		TreeView->SetItemExpansion(InItem, !TreeView->IsItemExpanded(InItem));
	}
}

void SLGUIPrefabPalette::AddComponentToSelectedActor(FItemPtr InItem)
{
	if (!InItem.IsValid() || !InItem->IsComponentClass())return;
	if (auto PrefabEditor = PrefabEditorPtr.Pin())
	{
		// reuse the shared drop path: it validates the parent (null / root agent) with
		// user-facing messages and handles the transaction
		PrefabEditor->HandleAssetsDropOnParentActor(
			TArray<FAssetData>{ FAssetData(InItem->ComponentClass.Get()) },
			PrefabEditor->GetCurrentSelectedActor());
	}
}

FString SLGUIPrefabPalette::GetItemFavoriteKey(FItemPtr InItem)const
{
	if (!InItem.IsValid())return FString();
	if (InItem->IsComponentClass())
	{
		return InItem->ComponentClass->GetClassPathName().ToString();
	}
	return InItem->Asset.GetSoftObjectPath().ToString();
}

TSharedPtr<SWidget> SLGUIPrefabPalette::OnContextMenuOpening()
{
	auto SelectedItems = TreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1 || SelectedItems[0]->IsCategory())
	{
		return nullptr;
	}
	FItemPtr Item = SelectedItems[0];

	FMenuBuilder MenuBuilder(true, nullptr);

	// component class rows get a reduced menu: favorite toggle + add-to-selected
	if (Item->IsComponentClass())
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("ComponentSection", "Component"));
		{
			const bool bIsClassFavorite = FavoritePaths.Contains(GetItemFavoriteKey(Item));
			MenuBuilder.AddMenuEntry(
				bIsClassFavorite ? LOCTEXT("RemoveFromFavorites", "Remove from Favorites") : LOCTEXT("AddToFavorites", "Add to Favorites"),
				LOCTEXT("ToggleFavoriteTooltip", "Favorited entries are pinned in the Favorites group at the top of the palette"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star"),
				FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::ToggleFavorite, Item)));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("AddToSelectedActor", "Add to Selected Actor"),
				LOCTEXT("AddToSelectedActorTooltip", "Add this component to the actor selected in the outliner (same as double-click)"),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
				FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::AddComponentToSelectedActor, Item)));
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}

	MenuBuilder.BeginSection(NAME_None, LOCTEXT("PrefabSection", "Prefab"));
	{
		const bool bIsFavorite = IsFavorite(Item->Asset);
		MenuBuilder.AddMenuEntry(
			bIsFavorite ? LOCTEXT("RemoveFromFavorites", "Remove from Favorites") : LOCTEXT("AddToFavorites", "Add to Favorites"),
			LOCTEXT("ToggleFavoriteTooltip2", "Favorited prefabs are pinned in the Favorites group at the top of the palette"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Star"),
			FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::ToggleFavorite, Item)));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("BrowseToAsset", "Browse to Asset"),
			LOCTEXT("BrowseToAssetTooltip", "Select this prefab in the Content Browser"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "SystemWideCommands.FindInContentBrowser"),
			FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::BrowseToAsset, Item)));

		const bool bIsHidden = IsAssetHiddenInPalette(Item->Asset);
		MenuBuilder.AddMenuEntry(
			bIsHidden ? LOCTEXT("ShowInPalette", "Show in Palette") : LOCTEXT("HideInPalette", "Hide in Palette"),
			LOCTEXT("ToggleHiddenTooltip", "Stored on the prefab asset (bHideInPalette). Hidden prefabs can be shown again via the palette's show-hidden filter or the Prefab Settings tab."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Visibility"),
			FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::ToggleItemHidden, Item)));

		MenuBuilder.AddSubMenu(
			LOCTEXT("SetCategory", "Set Category"),
			LOCTEXT("SetCategoryTooltip", "Set this prefab's palette category (stored on the prefab asset)"),
			FNewMenuDelegate::CreateLambda([this, Item](FMenuBuilder& SubMenuBuilder)
				{
					for (auto& Category : KnownCategories)
					{
						SubMenuBuilder.AddMenuEntry(
							LGUIPrefabPaletteLocal::GetCategoryDisplayText(Category),
							FText::GetEmpty(),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(this, &SLGUIPrefabPalette::SetItemCategory, Item, Category)));
					}
					SubMenuBuilder.AddSeparator();
					SubMenuBuilder.AddWidget(
						SNew(SBox)
						.Padding(FMargin(8, 2))
						.MinDesiredWidth(150)
						[
							SNew(SEditableTextBox)
							.HintText(LOCTEXT("NewCategoryHint", "New category..."))
							.OnTextCommitted(FOnTextCommitted::CreateSP(this, &SLGUIPrefabPalette::OnNewCategoryTextCommitted, Item))
						],
						FText::GetEmpty(), true, false);
				}));
	}
	MenuBuilder.EndSection();
	return MenuBuilder.MakeWidget();
}

void SLGUIPrefabPalette::BrowseToAsset(FItemPtr InItem)
{
	if (InItem.IsValid() && !InItem->IsCategory())
	{
		auto& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(TArray<FAssetData>{ InItem->Asset });
	}
}

void SLGUIPrefabPalette::SetItemCategory(FItemPtr InItem, FString NewCategory)
{
	if (!InItem.IsValid() || InItem->IsCategory())return;
	// explicit user action -- loading the single asset is acceptable here
	if (auto Prefab = Cast<ULGUIPrefab>(InItem->Asset.GetAsset()))
	{
#if WITH_EDITORONLY_DATA
		if (Prefab->PaletteCategory != NewCategory)
		{
			FScopedTransaction Transaction(LOCTEXT("SetPaletteCategoryTransaction", "Set Prefab Palette Category"));
			Prefab->Modify();
			Prefab->PaletteCategory = NewCategory;
			Prefab->MarkPackageDirty();
		}
#endif
		RequestRebuild();
	}
}

void SLGUIPrefabPalette::OnNewCategoryTextCommitted(const FText& InText, ETextCommit::Type CommitType, FItemPtr InItem)
{
	if (CommitType == ETextCommit::OnEnter)
	{
		FSlateApplication::Get().DismissAllMenus();
		SetItemCategory(InItem, InText.ToString().TrimStartAndEnd());
	}
}

bool SLGUIPrefabPalette::IsAssetHiddenInPalette(const FAssetData& InAssetData)const
{
	// loaded assets may have an unsaved edit -- prefer the live property
	if (InAssetData.IsAssetLoaded())
	{
		if (auto Prefab = Cast<ULGUIPrefab>(InAssetData.GetAsset()))
		{
#if WITH_EDITORONLY_DATA
			return Prefab->bHideInPalette;
#endif
		}
	}
	return InAssetData.GetTagValueRef<FString>(TEXT("bHideInPalette")) == TEXT("True");
}

void SLGUIPrefabPalette::ToggleItemHidden(FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->IsCategory())return;
	// explicit user action -- loading the single asset is acceptable here
	if (auto Prefab = Cast<ULGUIPrefab>(InItem->Asset.GetAsset()))
	{
#if WITH_EDITORONLY_DATA
		FScopedTransaction Transaction(LOCTEXT("ToggleHideInPaletteTransaction", "Toggle Prefab Hide In Palette"));
		Prefab->Modify();
		Prefab->bHideInPalette = !Prefab->bHideInPalette;
		Prefab->MarkPackageDirty();
#endif
		RequestRebuild();
	}
}

bool SLGUIPrefabPalette::IsFavorite(const FAssetData& InAssetData)const
{
	return FavoritePaths.Contains(InAssetData.GetSoftObjectPath().ToString());
}

void SLGUIPrefabPalette::ToggleFavorite(FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->IsCategory())return;
	const FString Key = GetItemFavoriteKey(InItem);
	if (FavoritePaths.Contains(Key))
	{
		FavoritePaths.Remove(Key);
	}
	else
	{
		FavoritePaths.Add(Key);
	}
	SaveFavorites();
	RequestRebuild();
}

void SLGUIPrefabPalette::LoadFavorites()
{
	FavoritePaths.Reset();
	TArray<FString> Paths;
	GConfig->GetArray(LGUIPrefabPaletteLocal::FavoritesConfigSection, LGUIPrefabPaletteLocal::FavoritesConfigKey, Paths, GEditorPerProjectIni);
	FavoritePaths.Append(Paths);
}

void SLGUIPrefabPalette::SaveFavorites()const
{
	GConfig->SetArray(LGUIPrefabPaletteLocal::FavoritesConfigSection, LGUIPrefabPaletteLocal::FavoritesConfigKey, FavoritePaths.Array(), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

void SLGUIPrefabPalette::OnAssetChanged(const FAssetData& InAssetData)
{
	// cheap class check; fires for every asset in the project
	if (InAssetData.AssetClassPath == ULGUIPrefab::StaticClass()->GetClassPathName())
	{
		RequestRebuild();
	}
}

void SLGUIPrefabPalette::OnAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	OnAssetChanged(InAssetData);
}

void SLGUIPrefabPalette::OnAssetRegistryFilesLoaded()
{
	RequestRebuild();
}

#undef LOCTEXT_NAMESPACE
