// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditorOutliner.h"
#include "SceneOutlinerModule.h"
#include "Modules/ModuleManager.h"
#include "GameFramework/Actor.h"
#include "Widgets/Layout/SBox.h"
#include "SceneOutlinerDelegates.h"
#include "ActorTreeItem.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "SceneOutliner/LGUISceneOutlinerInfoColumn.h"
#include "SOutlinerTreeView.h"
#include "Editor/GroupActor.h"
#include "LGUIPrefabEditor.h"
#include "LGUIEditorModule.h"
#include "SceneOutlinerStandaloneTypes.h"
#include "SceneOutlinerDragDrop.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "PrefabSystem/LGUIPrefabHelperObject.h"
#include "ActorBrowsingMode.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "SLGUIPrefabPalette.h"//FLGUIElementTemplateDragDropOp
#include "Core/ActorComponent/UIItem.h"
#include "Core/LGUILifeCycleBehaviour.h"
#include "Styling/SlateIconFinder.h"
#include "LGUIPrefabBehaviourUtils.h"
#include "LGUIPrefabEditorCommand.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Editor.h"
#include "Core/Actor/UIContainerActor.h"
#include "Core/Actor/UISpriteActor.h"
#include "Core/Actor/UIProceduralRectActor.h"
#include "Core/Actor/UITextureActor.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorOutliner"

#include "LGUI.h"//LGUI_CAN_DISABLE_OPTIMIZATION
#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

/**
 * Actor browser mode for the Prefab Editor outliner. Identical to the stock actor browser,
 * plus: asset drags (FAssetDragDropOp, e.g. rows from the Prefab Palette or the Content
 * Browser) can be dropped onto an actor row to create the assets under that actor -- same
 * as UMG's palette-to-hierarchy drop. Everything else falls through to FActorBrowsingMode.
 */
class FLGUIPrefabOutlinerMode : public FActorBrowsingMode
{
public:
	FLGUIPrefabOutlinerMode(SSceneOutliner* InSceneOutliner, TWeakPtr<FLGUIPrefabEditor> InPrefabEditor, TWeakObjectPtr<UWorld> InSpecifiedWorldToDisplay)
		: FActorBrowsingMode(InSceneOutliner, InSpecifiedWorldToDisplay)
		, PrefabEditorPtr(InPrefabEditor)
	{}

	virtual bool ParseDragDrop(FSceneOutlinerDragDropPayload& OutPayload, const FDragDropOperation& Operation) const override
	{
		if (Operation.IsOfType<FAssetDragDropOp>())
		{
			// accept: the assets travel via Payload.SourceOperation, DraggedItems stays empty.
			// Returning false here would short-circuit HandleDrop before ValidateDrop/OnDrop.
			return true;
		}
		return FActorBrowsingMode::ParseDragDrop(OutPayload, Operation);
	}

	virtual FSceneOutlinerDragValidationInfo ValidateDrop(const ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload) const override
	{
		if (Payload.SourceOperation.IsOfType<FAssetDragDropOp>())
		{
			if (auto ActorItem = DropTarget.CastTo<FActorTreeItem>())
			{
				AActor* TargetActor = ActorItem->Actor.Get();
				if (TargetActor != nullptr && !FLGUIPrefabEditor::ActorIsRootAgent(TargetActor))
				{
					// element templates (palette Panels/Layouts rows) show their display name
					if (Payload.SourceOperation.IsOfType<FLGUIElementTemplateDragDropOp>())
					{
						auto& TemplateOp = static_cast<const FLGUIElementTemplateDragDropOp&>(Payload.SourceOperation);
						return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::CompatibleAttach
							, FText::Format(LOCTEXT("DropTemplateOnActor", "Add {0} under {1}")
								, TemplateOp.TemplateDisplayName, FText::FromString(TargetActor->GetActorLabel())));
					}
					// distinguish "add component to X" from "add child under X" in the hover hint
					auto& AssetOp = static_cast<const FAssetDragDropOp&>(Payload.SourceOperation);
					bool bAllComponentClasses = AssetOp.GetAssets().Num() > 0;
					for (const FAssetData& AssetData : AssetOp.GetAssets())
					{
						UClass* AsClass = AssetData.IsAssetLoaded() ? Cast<UClass>(AssetData.GetAsset()) : nullptr;
						if (AsClass == nullptr || !AsClass->IsChildOf(UActorComponent::StaticClass()))
						{
							bAllComponentClasses = false;
							break;
						}
					}
					return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::CompatibleAttach
						, FText::Format(bAllComponentClasses
							? LOCTEXT("DropComponentOnActor", "Add component to {0}")
							: LOCTEXT("DropAssetOnActor", "Add under {0}")
							, FText::FromString(TargetActor->GetActorLabel())));
				}
			}
			return FSceneOutlinerDragValidationInfo(ESceneOutlinerDropCompatibility::IncompatibleGeneric
				, LOCTEXT("DropAssetInvalidTarget", "Drop on a UI actor to add the asset under it"));
		}
		return FActorBrowsingMode::ValidateDrop(DropTarget, Payload);
	}

	virtual void OnDrop(ISceneOutlinerTreeItem& DropTarget, const FSceneOutlinerDragDropPayload& Payload, const FSceneOutlinerDragValidationInfo& ValidationInfo) const override
	{
		if (Payload.SourceOperation.IsOfType<FAssetDragDropOp>())
		{
			if (auto ActorItem = DropTarget.CastTo<FActorTreeItem>())
			{
				if (AActor* TargetActor = ActorItem->Actor.Get())
				{
					if (auto PrefabEditor = PrefabEditorPtr.Pin())
					{
						auto& AssetOp = static_cast<const FAssetDragDropOp&>(Payload.SourceOperation);
						// element templates label the created actor with their display name
						FText CreatedActorLabel;
						if (Payload.SourceOperation.IsOfType<FLGUIElementTemplateDragDropOp>())
						{
							CreatedActorLabel = static_cast<const FLGUIElementTemplateDragDropOp&>(Payload.SourceOperation).TemplateDisplayName;
						}
						PrefabEditor->HandleAssetsDropOnParentActor(AssetOp.GetAssets(), TargetActor, CreatedActorLabel);
					}
				}
			}
			return;
		}
		FActorBrowsingMode::OnDrop(DropTarget, Payload, ValidationInfo);
	}

	virtual TSharedPtr<SWidget> CreateContextMenu() override
	{
		auto PrefabEditor = PrefabEditorPtr.Pin();
		if (!PrefabEditor.IsValid())
		{
			return FActorBrowsingMode::CreateContextMenu();
		}
		if (GEditor->GetSelectedActorCount() == 0)
		{
			return nullptr;
		}

		// build from the prefab editor's toolkit command list, so entries show their key bindings
		// and share CanExecute/Execute with the viewport shortcuts. The Edit section uses the
		// engine generic commands (Ctrl+X/C/V/W) mapped in BindCommands.
		const FLGUIPrefabEditorCommand& Commands = FLGUIPrefabEditorCommand::Get();
		FMenuBuilder MenuBuilder(true, PrefabEditor->GetToolkitCommands());
		MenuBuilder.BeginSection("LGUIPrefabOutlinerEdit", LOCTEXT("OutlinerEditSection", "Edit"));
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
		}
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("LGUIPrefabOutlinerWrap", LOCTEXT("OutlinerWrapSection", "Hierarchy"));
		{
			// UMG-style Wrap With: new parent container sized to the selection, selection reparented into it
			MenuBuilder.AddSubMenu(
				LOCTEXT("WrapWithSubMenu", "Wrap With..."),
				LOCTEXT("WrapWithSubMenuTooltip", "Create a new UI element sized to the selection and move the selected elements into it"),
				FNewMenuDelegate::CreateLambda([WeakEditor = PrefabEditorPtr](FMenuBuilder& SubMenu)
					{
						UClass* WrapperClasses[] =
						{
							AUIContainerActor::StaticClass(),
							AUISpriteActor::StaticClass(),
							AUIProceduralRectActor::StaticClass(),
							AUITextureActor::StaticClass(),
						};
						for (UClass* WrapperClass : WrapperClasses)
						{
							FString ShortName = WrapperClass->GetName();
							ShortName.RemoveFromEnd(TEXT("Actor"));
							SubMenu.AddMenuEntry(
								FText::FromString(ShortName),
								WrapperClass->GetToolTipText(),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([WeakEditor, WrapperClass]()
									{
										if (auto Editor = WeakEditor.Pin())
										{
											Editor->WrapSelectedUIItems(WrapperClass);
										}
									})));
						}
					}));
		}
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("LGUIPrefabOutlinerBehaviour", LOCTEXT("OutlinerBehaviourSection", "Behaviour"));
		{
			// UMG "Is Variable" counterpart: bind the selected element (actor or one of its
			// components) to a member variable on the prefab's companion behaviour blueprint
			AActor* SelectedActor = PrefabEditor->GetCurrentSelectedActor();
			const bool bSelectionValidForBehaviour = SelectedActor != nullptr
				&& !FLGUIPrefabEditor::ActorIsRootAgent(SelectedActor);
			// Promote excludes the prefab root (a variable to itself is meaningless); Event
			// handlers do NOT -- a prefab whose root IS the interactive element (root
			// UIButton.OnClick) is a common case that must be wireable.
			const bool bSelectionIsPromotable = bSelectionValidForBehaviour
				&& SelectedActor != PrefabEditor->GetPrefabManagerObject()->LoadedRootActor;
			if (bSelectionIsPromotable)
			{
				MenuBuilder.AddSubMenu(
					LOCTEXT("PromoteSubMenu", "Promote to Behaviour Variable"),
					LOCTEXT("PromoteSubMenuTooltip", "Add a variable to this prefab's behaviour blueprint (created on demand) and bind it to the selected element. The reference is saved with the prefab, so no runtime lookup is needed."),
					FNewMenuDelegate::CreateLambda([WeakEditor = PrefabEditorPtr, WeakActor = TWeakObjectPtr<AActor>(SelectedActor)](FMenuBuilder& SubMenu)
						{
							auto AddTargetEntry = [&SubMenu, WeakEditor](UObject* Target, const FText& Label)
								{
									SubMenu.AddMenuEntry(
										Label,
										FText::FromString(Target->GetClass()->GetPathName()),
										FSlateIconFinder::FindIconForClass(Target->GetClass()),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, WeakTarget = TWeakObjectPtr<UObject>(Target)]()
											{
												auto Editor = WeakEditor.Pin();
												if (Editor.IsValid() && WeakTarget.IsValid())
												{
													Editor->PromoteToBehaviourVariable(WeakTarget.Get());
												}
											})));
								};
							AActor* Actor = WeakActor.Get();
							if (Actor == nullptr)return;
							// the components carry the useful APIs (SetText/OnClick/...), the actor
							// itself is the fallback for hierarchy-level operations
							for (UActorComponent* Comp : Actor->GetComponents())
							{
								if (Comp == nullptr)continue;
								if (!Comp->IsA<UUIItem>() && !Comp->IsA<ULGUILifeCycleBehaviour>())continue;
								AddTargetEntry(Comp, FText::Format(LOCTEXT("PromoteAsComponent", "As {0} ({1})")
									, Comp->GetClass()->GetDisplayNameText(), FText::FromString(Comp->GetName())));
							}
							SubMenu.AddSeparator();
							AddTargetEntry(Actor, FText::Format(LOCTEXT("PromoteAsActor", "As Actor ({0})")
								, Actor->GetClass()->GetDisplayNameText()));
						}));
			}

			// UMG "Event +" counterpart: generate a handler on the companion behaviour
			// blueprint and wire an event (OnClick / OnToggle / ...) to it. Shown whenever the
			// selection has LGUIEventDelegate events -- including the prefab root itself.
			if (bSelectionValidForBehaviour)
			{
				TArray<LGUIPrefabBehaviourUtils::FDiscoveredEvent> DiscoveredEvents;
				LGUIPrefabBehaviourUtils::DiscoverEvents(SelectedActor, DiscoveredEvents);
				if (DiscoveredEvents.Num() > 0)
				{
					MenuBuilder.AddSubMenu(
						LOCTEXT("AddEventSubMenu", "Add Event Handler"),
						LOCTEXT("AddEventSubMenuTooltip", "Generate a handler function on this prefab's behaviour blueprint (created on demand) and wire the selected element's event to it, then jump to it -- UMG's event \"+\"."),
						FNewMenuDelegate::CreateLambda([WeakEditor = PrefabEditorPtr, DiscoveredEvents](FMenuBuilder& SubMenu)
							{
								for (const auto& Event : DiscoveredEvents)
								{
									if (Event.Component == nullptr)continue;
									SubMenu.AddMenuEntry(
										FText::Format(LOCTEXT("AddEventEntry", "{0} ({1})")
											, FText::FromString(Event.DisplayName), FText::FromString(Event.Component->GetClass()->GetName())),
										FText::Format(LOCTEXT("AddEventEntryTooltip", "Create a handler for {0} on the behaviour blueprint and bind it."), FText::FromString(Event.DisplayName)),
										FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, Event]()
											{
												if (auto Editor = WeakEditor.Pin())
												{
													Editor->AddEventHandler(Event);
												}
											})));
								}
							}));
				}
			}
		}
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("LGUIPrefabOutlinerAlign", LOCTEXT("OutlinerAlignSection", "Align"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("AlignSubMenu", "Align / Distribute"),
				LOCTEXT("AlignSubMenuTooltip", "Align or evenly distribute the selected UI elements"),
				FNewMenuDelegate::CreateLambda([&Commands](FMenuBuilder& SubMenu)
					{
						SubMenu.AddMenuEntry(Commands.AlignLeft);
						SubMenu.AddMenuEntry(Commands.AlignHCenter);
						SubMenu.AddMenuEntry(Commands.AlignRight);
						SubMenu.AddSeparator();
						SubMenu.AddMenuEntry(Commands.AlignTop);
						SubMenu.AddMenuEntry(Commands.AlignVMiddle);
						SubMenu.AddMenuEntry(Commands.AlignBottom);
						SubMenu.AddSeparator();
						SubMenu.AddMenuEntry(Commands.DistributeHorizontal);
						SubMenu.AddMenuEntry(Commands.DistributeVertical);
					}));
		}
		MenuBuilder.EndSection();
		MenuBuilder.BeginSection("LGUIPrefabOutlinerDelete", LOCTEXT("OutlinerDeleteSection", "Delete"));
		{
			MenuBuilder.AddMenuEntry(Commands.DestroyActor);
			MenuBuilder.AddMenuEntry(Commands.DestroyActorKeepChildren);
		}
		MenuBuilder.EndSection();
		return MenuBuilder.MakeWidget();
	}

	virtual FReply OnKeyDown(const FKeyEvent& InKeyEvent) override
	{
		// Shift+Delete = delete keeping children. Must be intercepted BEFORE the base class:
		// FActorBrowsingMode treats any Delete press (regardless of modifiers) as plain delete.
		if (InKeyEvent.GetKey() == EKeys::Delete && InKeyEvent.IsShiftDown())
		{
			if (auto PrefabEditor = PrefabEditorPtr.Pin())
			{
				PrefabEditor->DeleteSelectedActors_KeepChildren();
				return FReply::Handled();
			}
		}
		return FActorBrowsingMode::OnKeyDown(InKeyEvent);
	}

private:
	TWeakPtr<FLGUIPrefabEditor> PrefabEditorPtr;
};

FLGUIPrefabEditorOutliner::~FLGUIPrefabEditorOutliner()
{
	USelection::SelectionChangedEvent.RemoveAll(this);
}

void FLGUIPrefabEditorOutliner::InitOutliner(UWorld* World, TSharedPtr<FLGUIPrefabEditor> InPrefabEditorPtr, const TSet<AActor*>& InUnexpendActorSet)
{
	CurrentWorld = World;
	PrefabEditorPtr = InPrefabEditorPtr;
	if (CurrentWorld == nullptr)
	{
		return;
	}

	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");

	FSceneOutlinerInitializationOptions InitOptions;
	InitOptions.bShowTransient = false;
	InitOptions.bFocusSearchBoxWhenOpened = false;
	InitOptions.bShowCreateNewFolder = false;
	InitOptions.ColumnMap.Add(LGUISceneOutliner::FLGUISceneOutlinerInfoColumn::GetID(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 2));
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Gutter(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 0));
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 1));
	InitOptions.ColumnMap.Add(FSceneOutlinerBuiltInColumnTypes::ActorInfo(), FSceneOutlinerColumnInfo(ESceneOutlinerColumnVisibility::Visible, 10));
	if (ActorFilter.IsBound())
	{
		InitOptions.Filters->AddFilterPredicate<FActorTreeItem>(ActorFilter);
	}
	InitOptions.OutlinerIdentifier = "LGUIPrefabEditorOutliner";
	InitOptions.CustomDelete = FCustomSceneOutlinerDeleteDelegate::CreateRaw(this, &FLGUIPrefabEditorOutliner::OnDelete);

	// same as CreateActorBrowser, but with our mode subclass so asset drags (Prefab Palette /
	// Content Browser) can be dropped onto actor rows; columns are already set up above
	InitOptions.ModeFactory = FCreateSceneOutlinerMode::CreateLambda(
		[WeakPrefabEditor = TWeakPtr<FLGUIPrefabEditor>(InPrefabEditorPtr), WeakWorld = TWeakObjectPtr<UWorld>(World)](SSceneOutliner* Outliner)
		{
			return static_cast<ISceneOutlinerMode*>(new FLGUIPrefabOutlinerMode(Outliner, WeakPrefabEditor, WeakWorld));
		});
	TSharedRef<ISceneOutliner> SceneOutlinerRef = SceneOutlinerModule.CreateSceneOutliner(InitOptions);
	SceneOutlinerPtr = StaticCastSharedRef<SSceneOutliner>(SceneOutlinerRef->AsShared());

	//SceneOutlinerPtr->GetOnItemSelectionChanged().AddRaw(this, &FLGUIPrefabEditorOutliner::OnSceneOutlinerSelectionChanged);
	SceneOutlinerPtr->GetDoubleClickEvent().AddRaw(this, &FLGUIPrefabEditorOutliner::OnSceneOutlinerDoubleClick);
	GEditor->OnLevelActorListChanged().AddLambda([SceneOutlinerWeak = TWeakPtr<SSceneOutliner>(SceneOutlinerPtr)]() {//UE5 not auto refresh the actor label display, so manually refresh it
		if (SceneOutlinerWeak.IsValid())
		{
			SceneOutlinerWeak.Pin()->Refresh();
		}
		});
	FCoreDelegates::OnActorLabelChanged.AddLambda([SceneOutlinerWeak = TWeakPtr<SSceneOutliner>(SceneOutlinerPtr)](AActor* actor) {//UE5 not auto refresh the actor label display, so manually refresh it
		if (SceneOutlinerWeak.IsValid())
		{
			SceneOutlinerWeak.Pin()->FullRefresh();
		}
	});
	

	auto& TreeView = SceneOutlinerPtr->GetTree();
	TSet<FSceneOutlinerTreeItemPtr> VisitingItems;
	TreeView.GetExpandedItems(VisitingItems);
	auto& MutableTreeView = const_cast<STreeView<FSceneOutlinerTreeItemPtr>&>(TreeView);
	for (auto& Item : VisitingItems)
	{
		if (auto ActorTreeItem = Item->CastTo<FActorTreeItem>())
		{
			if (ActorTreeItem->Actor.IsValid())
			{
				if (InUnexpendActorSet.Contains(ActorTreeItem->Actor.Get()))
				{
					MutableTreeView.SetItemExpansion(Item, false);
				}
			}
		}
	}
	SceneOutlinerPtr->Refresh();

	OutlinerWidget =
		SNew(SBox)
		.WidthOverride(OutlinerWidth)
		.HeightOverride(OutlinerHeight)
		[
			SceneOutlinerRef
		];



	USelection::SelectionChangedEvent.AddRaw(this, &FLGUIPrefabEditorOutliner::OnEditorSelectionChanged);
}

void FLGUIPrefabEditorOutliner::UnexpandActorForDragDroppedPrefab(AActor* InActor)
{
	auto& TreeView = SceneOutlinerPtr->GetTree();
	TSet<FSceneOutlinerTreeItemPtr> VisitingItems;
	TreeView.GetExpandedItems(VisitingItems);
	auto& MutableTreeView = const_cast<STreeView<FSceneOutlinerTreeItemPtr>&>(TreeView);
	for (auto& Item : VisitingItems)
	{
		if (auto ActorTreeItem = Item->CastTo<FActorTreeItem>())
		{
			if (ActorTreeItem->Actor.IsValid())
			{
				if (ActorTreeItem->Actor->IsAttachedTo(InActor) || ActorTreeItem->Actor.Get() == InActor)
				{
					MutableTreeView.SetItemExpansion(Item, false);
				}
			}
		}
	}
}

void FLGUIPrefabEditorOutliner::OnDelete(const TArray<TWeakPtr<ISceneOutlinerTreeItem>>& InSelectedTreeItemArray)
{
	TArray<TWeakObjectPtr<AActor>> InSelectedActorArray;
	for (auto Item : InSelectedTreeItemArray)
	{
		if (Item.IsValid())
		{
			if (auto Actor = GetActorFromTreeItem(Item.Pin()))
			{
				InSelectedActorArray.Add(Actor);
			}
		}
	}
	PrefabEditorPtr.Pin()->DeleteActors(InSelectedActorArray);
}

AActor* FLGUIPrefabEditorOutliner::GetActorFromTreeItem(FSceneOutlinerTreeItemPtr TreeItem)const
{
	if (auto ActorTreeItem = TreeItem->CastTo<FActorTreeItem>())
	{
		if (ActorTreeItem->Actor.IsValid() && !ActorTreeItem->Actor->IsPendingKillPending())
		{
			if (ActorTreeItem->Actor->GetWorld())
			{
				return Cast<AActor>(ActorTreeItem->Actor.Get());
			}
		}
	}
	return nullptr;
}

void FLGUIPrefabEditorOutliner::OnSceneOutlinerDoubleClick(FSceneOutlinerTreeItemPtr ItemPtr)
{
	if (OnActorDoubleClickDelegate.IsBound())
	{
		FActorTreeItem* ActorTreeItem = (FActorTreeItem*)ItemPtr.Get();
		if (ActorTreeItem)
		{
			OnActorDoubleClickDelegate.ExecuteIfBound(ActorTreeItem->Actor.Get());
		}
	}
}

void FLGUIPrefabEditorOutliner::OnEditorSelectionChanged(UObject* Object)
{
	if (!SceneOutlinerPtr.IsValid())
	{
		return;
	}

	USelection* Selection = Cast<USelection>(Object);
	if (Selection)
	{
		if (AActor* Actor = Selection->GetTop<AActor>())
		{
			if (Actor->GetWorld() != CurrentWorld.Get())
			{
				return;
			}
			OnActorPickedDelegate.ExecuteIfBound(Actor);

			SceneOutlinerPtr->SetSelection([=](ISceneOutlinerTreeItem& TreeItem) {
				if (auto ActorTree = TreeItem.CastTo<FActorTreeItem>())
				{
					return ActorTree->Actor.Get() == Actor;
				}
				return false;
				});

			SelectedActor = Actor;
		}
		else
		{
			SceneOutlinerPtr->ClearSelection();
			SelectedActor = nullptr;
		}
	}
}

void FLGUIPrefabEditorOutliner::Refresh()
{
	if (SceneOutlinerPtr.IsValid())
	{
		SceneOutlinerPtr->Refresh();
	}
	else
	{
	}
}

void FLGUIPrefabEditorOutliner::FullRefresh()
{
	if (SceneOutlinerPtr.IsValid())
	{
		SceneOutlinerPtr->FullRefresh();

	}
	else
	{
	}
}

void FLGUIPrefabEditorOutliner::ClearSelectedActor()
{
	SelectedActor = nullptr;
}

void FLGUIPrefabEditorOutliner::GetUnexpendActor(TArray<AActor*>& InOutAllActors)const
{
	auto& TreeView = SceneOutlinerPtr->GetTree();
	TSet<FSceneOutlinerTreeItemPtr> VisitingItems;
	TreeView.GetExpandedItems(VisitingItems);
	for (auto& Item : VisitingItems)
	{
		if (auto ActorTreeItem = Item->CastTo<FActorTreeItem>())
		{
			if (ActorTreeItem->Actor.IsValid())
			{
				InOutAllActors.Remove(ActorTreeItem->Actor.Get());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif

