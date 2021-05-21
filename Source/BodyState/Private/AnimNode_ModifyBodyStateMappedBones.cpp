// Copyright 1998-2020 Epic Games, Inc. All Rights Reserved.

#include "AnimNode_ModifyBodyStateMappedBones.h"

#include "AnimationRuntime.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Runtime/Engine/Public/Animation/AnimInstanceProxy.h"
#include "Skeleton/BodyStateArm.h"
FAnimNode_ModifyBodyStateMappedBones::FAnimNode_ModifyBodyStateMappedBones() : FAnimNode_SkeletalControlBase()
{
	WorldIsGame = false;
	Alpha = 1.f;
}

void FAnimNode_ModifyBodyStateMappedBones::OnInitializeAnimInstance(
	const FAnimInstanceProxy* InProxy, const UAnimInstance* InAnimInstance)
{
	Super::OnInitializeAnimInstance(InProxy, InAnimInstance);
	BSAnimInstance = Cast<UBodyStateAnimInstance>(InAnimInstance);
}

void FAnimNode_ModifyBodyStateMappedBones::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	EvaluateComponentPose_AnyThread(Output);
}
void FAnimNode_ModifyBodyStateMappedBones::ApplyTranslation(const FCachedBoneLink& CachedBone, FTransform& NewBoneTM)
{
	if (MappedBoneAnimData.bShouldDeformMesh)
	{
		const FVector& BoneTranslation = CachedBone.BSBone->BoneData.Transform.GetTranslation();
		FQuat AdditionalRotation = MappedBoneAnimData.AutoCorrectRotation * MappedBoneAnimData.OffsetTransform.GetRotation();

		const FVector RotatedTranslation = AdditionalRotation.RotateVector(BoneTranslation);
		NewBoneTM.SetTranslation(RotatedTranslation + MappedBoneAnimData.OffsetTransform.GetLocation());
	}
	// wrist only, removes the need for a wrist modify node in the anim blueprint
	else
	{
		if (CachedBone.BSBone->Name.Contains("wrist"))
		{
			FVector BoneTranslation = CachedBone.BSBone->BoneData.Transform.GetTranslation();
			if (MappedBoneAnimData.IsFlippedByScale)
			{
				BoneTranslation.X = -BoneTranslation.X;
			}
			const FVector RotatedTranslation = MappedBoneAnimData.OffsetTransform.GetRotation().RotateVector(BoneTranslation);
			NewBoneTM.SetTranslation(RotatedTranslation + MappedBoneAnimData.OffsetTransform.GetLocation());
		}
	}
}
void FAnimNode_ModifyBodyStateMappedBones::ApplyRotation(const FCachedBoneLink& CachedBone, FTransform& NewBoneTM)
{
	FQuat BoneQuat = CachedBone.BSBone->BoneData.Transform.GetRotation();

	// Apply pre and post adjustment (Post * (Input * Pre) )
	if (!MappedBoneAnimData.PreBaseRotation.ContainsNaN())
	{
		BoneQuat = MappedBoneAnimData.AutoCorrectRotation * (MappedBoneAnimData.OffsetTransform.GetRotation() *
																(BoneQuat * MappedBoneAnimData.PreBaseRotation.Quaternion()));
	}

	NewBoneTM.SetRotation(BoneQuat);
}
bool FAnimNode_ModifyBodyStateMappedBones::CalcIsTracking()
{
	bool IsTracking = false;
	switch (BSAnimInstance->AutoMapTarget)
	{
		case EBodyStateAutoRigType::HAND_LEFT:
		{
			IsTracking = MappedBoneAnimData.BodyStateSkeleton->LeftArm()->Hand->Wrist->IsTracked();
		}
		break;
		case EBodyStateAutoRigType::HAND_RIGHT:
		{
			IsTracking = MappedBoneAnimData.BodyStateSkeleton->RightArm()->Hand->Wrist->IsTracked();
		}
		break;
		case EBodyStateAutoRigType::BOTH_HANDS:
		{
			IsTracking = MappedBoneAnimData.BodyStateSkeleton->LeftArm()->Hand->Wrist->IsTracked() ||
						 MappedBoneAnimData.BodyStateSkeleton->RightArm()->Hand->Wrist->IsTracked();
		}
		break;
	}
	return IsTracking;
}
bool FAnimNode_ModifyBodyStateMappedBones::CheckInitEvaulate()
{
	if (!BSAnimInstance)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow,
				TEXT("Ultraleap Modify Mapped Bones - No Bodystate Anim Instance - Reparent your anim instance to "
					 "BodyStateAnimInstance"));
		}
		return false;
	}
	if (BSAnimInstance->MappedBoneList.Num() == 0)
	{
		return false;
	}
	if (MappedBoneAnimData.BoneMap.Num() == 0 || MappedBoneAnimData.BodyStateSkeleton == nullptr)
	{
		UE_LOG(LogTemp, Log, TEXT("Updating cached MappedBoneAnimData"));
		MappedBoneAnimData = BSAnimInstance->MappedBoneList[0];
	}

	if (!MappedBoneAnimData.BodyStateSkeleton)
	{
		return false;
	}
	return true;
}
bool FAnimNode_ModifyBodyStateMappedBones::CacheArmOrWrist(
	const FCachedBoneLink& CachedBone, FCachedBoneLink& ArmCachedBone, FCachedBoneLink& WristCachedBone)
{
	bool SkipBone = false;
	// based on the unity version, we use an offset from the wrist for the elbow
	// so skip, wait for wrist to be cached, then calc outside this loop
	// these names are constant as they're the bodystate names
	if (CachedBone.BSBone->Name.Contains("lowerarm"))
	{
		ArmCachedBone = CachedBone;
		SkipBone = true;
	}
	if (CachedBone.BSBone->Name.Contains("wrist"))
	{
		WristCachedBone = CachedBone;
	}

	return SkipBone;
}

void FAnimNode_ModifyBodyStateMappedBones::EvaluateComponentPose_AnyThread(FComponentSpacePoseContext& Output)
{
	Super::EvaluateComponentPose_AnyThread(Output);

	if (!CheckInitEvaulate())
	{
		return;
	}
	// If we don't have data driving us, ignore this evaluation
	const FBoneContainer& BoneContainer = Output.Pose.GetPose().GetBoneContainer();
	float BlendWeight = FMath::Clamp<float>(ActualAlpha, 0.f, 1.f);

	FScopeLock ScopeLock(&MappedBoneAnimData.BodyStateSkeleton->BoneDataLock);

	// used to be in the event graph for the anim blueprints
	// do nothing if not tracking
	if (!CalcIsTracking())
	{
		BlendWeight = 0;
	}

	// cached for elbow position
	FCachedBoneLink ArmCachedBone;
	FCachedBoneLink WristCachedBone;

	// SN: there should be an array re-ordered by hierarchy (parents -> children order)
	for (auto CachedBone : MappedBoneAnimData.CachedBoneList)
	{
		if (CachedBone.MeshBone.BoneIndex == -1)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s has an invalid bone index: %d"), *CachedBone.MeshBone.BoneName.ToString(),
				CachedBone.MeshBone.BoneIndex);
			continue;
		}

		// returns true if skipping (is the arm)
		if (CacheArmOrWrist(CachedBone, ArmCachedBone, WristCachedBone))
		{
			continue;
		}
		FCompactPoseBoneIndex CompactPoseBoneToModify = CachedBone.MeshBone.GetCompactPoseIndex(BoneContainer);
		FTransform NewBoneTM = Output.Pose.GetComponentSpaceTransform(CompactPoseBoneToModify);

		ApplyRotation(CachedBone, NewBoneTM);
		ApplyTranslation(CachedBone, NewBoneTM);

		// Set the transform back into the anim system
		TArray<FBoneTransform> TempTransform;
		TempTransform.Add(FBoneTransform(CachedBone.MeshBone.GetCompactPoseIndex(BoneContainer), NewBoneTM));
		Output.Pose.LocalBlendCSBoneTransforms(TempTransform, BlendWeight);
	}
	// Elbow position, only if lower arm bone found.
	if (WristCachedBone.MeshBone.BoneIndex > -1 && ArmCachedBone.MeshBone.BoneIndex > -1)
	{
		auto WristPosition = WristCachedBone.BSBone->BoneData.Transform.GetLocation();
		auto ElbowForward = FRotationMatrix(ArmCachedBone.BSBone->BoneData.Transform.Rotator()).GetScaledAxis(EAxis::X);
		auto ElbowPosition =
			WristPosition - ((MappedBoneAnimData.ElbowLength * ElbowForward) + MappedBoneAnimData.OffsetTransform.GetLocation());

		auto CachedBone = ArmCachedBone;
		FCompactPoseBoneIndex CompactPoseBoneToModify = CachedBone.MeshBone.GetCompactPoseIndex(BoneContainer);
		FTransform NewBoneTM;
		FTransform ComponentTransform;

		NewBoneTM = Output.Pose.GetComponentSpaceTransform(CompactPoseBoneToModify);

		ApplyRotation(CachedBone, NewBoneTM);
		ApplyTranslation(CachedBone, NewBoneTM);

		// Set the transform back into the anim system
		TArray<FBoneTransform> TempTransform;
		TempTransform.Add(FBoneTransform(CachedBone.MeshBone.GetCompactPoseIndex(BoneContainer), NewBoneTM));
		Output.Pose.LocalBlendCSBoneTransforms(TempTransform, BlendWeight);
	}
}

bool FAnimNode_ModifyBodyStateMappedBones::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return (MappedBoneAnimData.BodyStateSkeleton != nullptr);
}