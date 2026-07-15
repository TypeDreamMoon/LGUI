// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class UBlueprint;
class UActorComponent;
class UObject;
class ULGUIPrefab;

/**
 * The prefab's "companion behaviour blueprint", UMG-WidgetBlueprint style: one blueprint
 * class (ULGUILifeCycleBehaviour subclass) attached to the prefab root actor carries the
 * prefab's logic, and prefab elements bind to its variables as plain object references
 * serialized with the prefab (prefab-internal references are GUID-remapped on load, so
 * they survive renames and cost nothing at runtime).
 *
 * IMPORTANT serialization constraint: LGUI's prefab writer skips properties carrying
 * CPF_DisableEditOnInstance (see LGUIPrefab_ShouldSkipProperty), and blueprint variables
 * get that flag BY DEFAULT. Every variable this workflow creates therefore has the flag
 * cleared ("Instance Editable"), and the auto-bind pass refuses variables that still
 * carry it -- which doubles as the opt-out: untick Instance Editable and the variable
 * belongs to runtime code, not to this workflow.
 *
 * These helpers are the "logic host" backend for the Prefab Editor's behaviour workflow.
 * They intentionally expose backend-neutral operations (find / create / promote-variable /
 * auto-bind) so an alternative scripting backend (e.g. AngelScript classes, which are also
 * real UClasses) can implement the same operations later without touching the editor UI.
 */
namespace LGUIPrefabBehaviourUtils
{
	/** ComponentTags marker identifying the companion component (serialized with the prefab). */
	extern const FName CompanionComponentTag;

	/** The companion behaviour component on the root actor (tag-matched, NOT just any blueprint behaviour), or null. */
	UActorComponent* FindBehaviourComponent(AActor* InPrefabRootActor);
	/** The blueprint asset behind FindBehaviourComponent's result, or null. */
	UBlueprint* FindBehaviourBlueprint(AActor* InPrefabRootActor);
	/**
	 * Create "BP_<PrefabName>" (ULGUILifeCycleBehaviour subclass) next to the prefab asset
	 * and attach a tagged instance to the root actor. If a compatible orphaned asset with
	 * that name already exists (e.g. the component was lost without saving), it is re-attached
	 * instead of minting "BP_<PrefabName>1". Returns null on failure.
	 */
	UBlueprint* CreateBehaviourBlueprint(ULGUIPrefab* InPrefab, AActor* InPrefabRootActor);

	/**
	 * UMG "Is Variable" counterpart: add (or reuse, when type-compatible) an Instance-Editable
	 * member variable on the behaviour blueprint typed to InTarget's class, compile, then bind
	 * the behaviour instance's property to InTarget. When InVariableName exists with an
	 * incompatible type, "<Name>_<TargetClass>" is tried before giving up; when it exists and
	 * is already bound to a DIFFERENT object, the binding is replaced and OutMessage says so.
	 * @return true on success; OutMessage carries the failure reason otherwise.
	 */
	bool PromoteToVariable(UBlueprint* InBlueprint, AActor* InPrefabRootActor, UObject* InTarget, const FString& InVariableName, FText& OutMessage);

	/** Variable name suggestion: actor label (or component name) cleaned to a valid identifier (CJK kept). */
	FString MakeVariableNameForTarget(UObject* InTarget);

	/**
	 * Editor-time counterpart of UMG's BindWidget, materialized into serialized references:
	 * for every Instance-Editable blueprint-declared null object property on the root actor's
	 * behaviour components, find the actor in the prefab subtree whose (sanitized) label equals
	 * the property name and bind it (actor property -> the actor, component property -> that
	 * actor's first matching component). Ambiguous labels are reported instead of guessed at,
	 * and dangling references (bound object deleted or outside this prefab's subtree) are reported.
	 * @param OutBoundDetails  "Variable -> Element" line per auto-bound property.
	 * @param OutProblems      Dangling / ambiguous / not-savable descriptions.
	 */
	void AutoBindAndValidate(AActor* InPrefabRootActor, TArray<FString>& OutBoundDetails, TArray<FString>& OutProblems);
}
