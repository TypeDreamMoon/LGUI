// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIPrefabEditorCommand.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorCommand"

void FLGUIPrefabEditorCommand::RegisterCommands()
{
	UI_COMMAND(Apply, "Apply", "Apply changes to prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(RawDataViewer, "Prefab Settings", "Edit this prefab asset's own properties (palette category, hide in palette, references, raw data). Like UMG's Class Settings.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(OpenPrefabHelperObject, "PrefabHelperObject", "Open PrefabHelperObject details panel of this prefab.", EUserInterfaceActionType::Button, FInputChord());
	UI_COMMAND(CopyActor, "Copy Actors", "Copy selected actors with hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::C, EModifierKey::Shift | EModifierKey::Alt));
	UI_COMMAND(PasteActor, "Paste Actors", "Paste actors with hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::V, EModifierKey::Shift | EModifierKey::Alt));
	UI_COMMAND(CutActor, "Cut Actors", "Cut actors with hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::X, EModifierKey::Shift | EModifierKey::Alt));
	UI_COMMAND(DuplicateActor, "Duplicate Actors", "Duplicate selected actors with hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::D, EModifierKey::Shift | EModifierKey::Alt));
	UI_COMMAND(DestroyActor, "Destroy Actors", "Destroy selected actors with hierarchy", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete));
	UI_COMMAND(DestroyActorKeepChildren, "Destroy Actors (Keep Children)", "Destroy selected actors; their children are reparented to the destroyed actor's parent", EUserInterfaceActionType::Button, FInputChord(EKeys::Delete, EModifierKey::Shift));
}

#undef LOCTEXT_NAMESPACE