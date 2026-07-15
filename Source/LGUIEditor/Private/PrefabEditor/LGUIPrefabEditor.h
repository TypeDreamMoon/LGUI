// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"
#include "LGUIPrefabEditorScene.h"
#pragma once

class ULGUIPrefab;
class SLGUIPrefabEditorViewport;
class SLGUIPrefabEditorDetails;
class FLGUIPrefabEditorOutliner;
class SLGUIPrefabOverrideParameterEditor;
class SLGUIPrefabRawDataViewer;
class AActor;
class FLGUIPrefabEditorScene;
class ULGUIPrefabHelperObject;
class ULGUIPrefabOverrideParameterHelperObject;
class ULGUIPrefabOverrideHelperObject;
struct FLGUISubPrefabData;

/**
 * 
 */
class FLGUIPrefabEditor : public FAssetEditorToolkit
	, public FGCObject
	, public FEditorUndoClient
{
public:
	FLGUIPrefabEditor();
	~FLGUIPrefabEditor();

	// FEditorUndoClient interface: keep the editor consistent after global undo/redo.
	// Without this, undoing after Apply leaves the scene rolled back while the dirty flag
	// still says "clean", so closing the window silently loses the undone state.
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End FEditorUndoClient interface

	// IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& TabManager) override;
	// End of IToolkit interface

	// FAssetEditorToolkit
public:
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FString GetDocumentationLink() const override;
	virtual void OnToolkitHostingStarted(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void OnToolkitHostingFinished(const TSharedRef<class IToolkit>& Toolkit) override;
	virtual void SaveAsset_Execute()override;
private:
	virtual bool OnRequestClose()override;
	// End of FAssetEditorToolkit
public:
	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName()const { return TEXT("LGUIPrefabEditor"); }

	bool CheckBeforeSaveAsset();

	void InitPrefabEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, ULGUIPrefab* InPrefab);
	TArray<AActor*> GetAllActors();

	/** Try to handle a drag-drop operation */
	FReply TryHandleAssetDragDropOperation(const FDragDropEvent& DragDropEvent);
	/**
	 * Create actors / sub prefabs from dropped assets, attached under the given parent actor.
	 * Shared by the viewport drop (parent = selected actor) and the outliner row drop (parent = drop target row).
	 */
	FReply HandleAssetsDropOnParentActor(const TArray<struct FAssetData>& DroppedAssetData, AActor* InParentActor);

	FLGUIPrefabEditorScene& GetPreviewScene();
	UWorld* GetWorld();
	ULGUIPrefab* GetPrefabBeingEdited()const { return PrefabBeingEdited; }
	AActor* GetCurrentSelectedActor()const { return CurrentSelectedActor.Get(); }

	/**
	 * Delete actors (with validation: root, root agent and sub-prefab members are refused).
	 * @param bKeepChildren  When true, direct child actors that are not themselves being deleted
	 *                       are reparented to the deleted actor's parent instead of being destroyed.
	 *                       (Sub prefab roots always take their whole prefab with them.)
	 */
	void DeleteActors(const TArray<TWeakObjectPtr<AActor>>& InSelectedActorArray, bool bKeepChildren = false);
	/** Delete the currently selected actors, reparenting their children (Shift+Delete). */
	void DeleteSelectedActors_KeepChildren();

	/** UMG-designer-style layout commands operating on the selected UI elements (world space, UI plane = world Y/Z). */
	enum class EAlignType : uint8 { Left, HCenter, Right, Top, VMiddle, Bottom };
	void AlignSelectedUIItems(EAlignType InType);
	void DistributeSelectedUIItems(bool bHorizontal);
	/** Selected UUIItem roots in this editor's world (root agent excluded). */
	TArray<class UUIItem*> GetSelectedUIItems()const;

	/**
	 * Scan every FLGUIEventDelegate on the live actor tree and warn (notification) about bindings
	 * whose target no longer resolves (renamed component, deleted function). Bindings locate their
	 * target by actor + component NAME + function NAME, so renames break them silently at runtime.
	 */
	void ValidateEventBindings();

	static FLGUIPrefabEditor* GetEditorForPrefabIfValid(ULGUIPrefab* InPrefab);
	static ULGUIPrefabHelperObject* GetEditorPrefabHelperObjectForActor(AActor* InActor);
	static bool WorldIsPrefabEditor(UWorld* InWorld);
	static bool ActorIsRootAgent(AActor* InActor);
	static void IterateAllPrefabEditor(const TFunction<void(FLGUIPrefabEditor*)>& InFunction);
	bool RefreshOnSubPrefabDirty(ULGUIPrefab* InSubPrefab);

	bool GetSelectedObjectsBounds(FBoxSphereBounds& OutResult);
	FBoxSphereBounds GetAllObjectsBounds();
	bool ActorBelongsToSubPrefab(AActor* InSubPrefabActor);
	bool ActorIsSubPrefabRoot(AActor* InSubPrefabRootActor);
	FLGUISubPrefabData GetSubPrefabDataForActor(AActor* InSubPrefabActor);
	void GetInitialViewLocationAndRotation(FVector& OutLocation, FRotator& OutRotation, FVector& OutOrbitLocation);

	void OpenSubPrefab(AActor* InSubPrefabActor);
	void SelectSubPrefab(AActor* InSubPrefabActor);
	bool GetAnythingDirty()const;
	void CloseWithoutCheckDataDirty();

	ULGUIPrefabHelperObject* GetPrefabManagerObject()const { return PrefabHelperObject; }
	void ApplyPrefab();
private:
	TObjectPtr<ULGUIPrefab> PrefabBeingEdited = nullptr;
	TObjectPtr<ULGUIPrefabHelperObject> PrefabHelperObject = nullptr;
	static TArray<FLGUIPrefabEditor*> LGUIPrefabEditorInstanceCollection;

	TSharedPtr<SLGUIPrefabEditorViewport> ViewportPtr;
	TSharedPtr<SLGUIPrefabEditorDetails> DetailsPtr;
	TSharedPtr<FLGUIPrefabEditorOutliner> OutlinerPtr;
	TSharedPtr<SLGUIPrefabRawDataViewer> PrefabRawDataViewer;
	TSharedPtr<class SLGUIPrefabPalette> PalettePtr;

	TWeakObjectPtr<AActor> CurrentSelectedActor;

	FLGUIPrefabEditorScene PreviewScene;
private:

	void BindCommands();
	//void ExtendMenu();
	void ExtendToolbar();

	FText GetApplyButtonStatusTooltip()const;
	FSlateIcon GetApplyButtonStatusImage()const;

	void OnApply();
	void OnOpenRawDataViewerPanel();
	void OnOpenPrefabHelperObjectDetailsPanel();

	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Details(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Outliner(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PrefabRawDataViewer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PrefabPalette(const FSpawnTabArgs& Args);

	bool IsFilteredActor(const AActor* Actor);
	void OnOutlinerPickedChanged(AActor* Actor);
	void OnOutlinerActorDoubleClick(AActor* Actor);
	void HandleUndoRedo();
	/**
	 * Editor-scope isolation for clipboard commands: deselect actors that belong to other worlds
	 * (other prefab editors / the level editor) so the shared static tools only see this editor's
	 * selection, and paste lands in this world instead of wherever a stray selection points.
	 */
	void RestrictSelectionToThisWorld();
	bool HasSelectionInThisWorld()const;
};
