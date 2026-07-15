// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "LGUIPrefabSettings.generated.h"

/** for LGUIPrefab config */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="LGUI Prefab"))
class LGUI_API ULGUIPrefabSettings :public UDeveloperSettings
{
	GENERATED_BODY()
public:
	virtual FName GetCategoryName()const override { return TEXT("Plugins"); }
	/**
	 * For load prefab debug, display a log that shows how much time a LoadPrefab cost.
	 */
	UPROPERTY(EditAnywhere, config, Category = "LGUI")
		bool bLogPrefabLoadTime = false;
#if WITH_EDITORONLY_DATA
	/**
	 * On Apply in the Prefab Editor, additionally export a human-readable text snapshot of the
	 * prefab (actor/component hierarchy + non-default properties) to
	 * <Project>/PrefabTextSnapshots/, mirroring the asset path. Prefab assets are versioned
	 * binary blobs that cannot be diffed or merged -- commit these snapshots alongside them so
	 * code review and history show WHAT changed. Off by default.
	 */
	UPROPERTY(EditAnywhere, config, Category = "LGUI")
		bool bExportTextSnapshotOnApply = false;
#endif

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
public:
	static bool GetLogPrefabLoadTime();
};
