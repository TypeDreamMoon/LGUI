// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditorViewport.h"
#include "LGUIPrefabEditorViewportClient.h"
#include "LGUIPrefabEditor.h"
#include "LGUIPrefabEditorViewportToolbar.h"
#include "PrefabSystem/LGUIPrefab.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorViewport"

void SLGUIPrefabEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InPrefabEditor, EViewModeIndex InViewMode)
{
	this->PrefabEditorPtr = InPrefabEditor;
	this->ViewMode = InViewMode;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}
void SLGUIPrefabEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}
TSharedRef<FEditorViewportClient> SLGUIPrefabEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FLGUIPrefabEditorViewportClient(this->PrefabEditorPtr.Pin()->GetPreviewScene(), this->PrefabEditorPtr, SharedThis(this)));
	// restore the persisted viewport type; new prefabs default to the 2D canvas view (LVT_OrthoYZ)
	EditorViewportClient->ViewportType = (ELevelViewportType)this->PrefabEditorPtr.Pin()->GetPrefabBeingEdited()->PrefabDataForPrefabEditor.ViewportType;
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetRealtime(true);
	EditorViewportClient->SetShowStats(true);
	EditorViewportClient->VisibilityDelegate.BindLambda([]() {return true; });
	EditorViewportClient->SetViewMode(ViewMode);
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SLGUIPrefabEditorViewport::BuildViewportToolbar()
{
	return SNew(SLGUIPrefabEditorViewportToolbar, SharedThis(this));
}
EVisibility SLGUIPrefabEditorViewport::GetTransformToolbarVisibility() const
{
	return EVisibility::Visible;
}
void SLGUIPrefabEditorViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->FocusViewportToTargets();
}

TSharedRef<SEditorViewport> SLGUIPrefabEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}
TSharedPtr<FExtender> SLGUIPrefabEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}
void SLGUIPrefabEditorViewport::OnFloatingButtonClicked()
{

}

FReply SLGUIPrefabEditorViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return PrefabEditorPtr.Pin()->TryHandleAssetDragDropOperation(DragDropEvent);
}

void SLGUIPrefabEditorViewport::ToggleViewportType2D3D()
{
	if (!EditorViewportClient.IsValid())return;
	const ELevelViewportType NewType = IsViewport2D() ? LVT_Perspective : LVT_OrthoYZ;
	EditorViewportClient->SetViewportType(NewType);
	if (NewType == LVT_OrthoYZ)
	{
		// frame the canvas when entering 2D so the user isn't lost at an arbitrary ortho zoom
		EditorViewportClient->FocusViewportToTargets();
	}
	// persist immediately (also saved on Apply)
	if (auto PrefabEditor = PrefabEditorPtr.Pin())
	{
		if (auto Prefab = PrefabEditor->GetPrefabBeingEdited())
		{
			Prefab->PrefabDataForPrefabEditor.ViewportType = (uint8)NewType;
		}
	}
}

bool SLGUIPrefabEditorViewport::IsViewport2D() const
{
	return EditorViewportClient.IsValid() && EditorViewportClient->GetViewportType() != LVT_Perspective;
}

#undef LOCTEXT_NAMESPACE