/******************************************************************************
 * Copyright (C) Ultraleap, Inc. 2011-2021.                                   *
 *                                                                            *
 * Use subject to the terms of the Apache License 2.0 available at            *
 * http://www.apache.org/licenses/LICENSE-2.0, or another agreement           *
 * between Ultraleap and you, your company or other organization.             *
 ******************************************************************************/

#include "FUltraleapCombinedDeviceConfidence.h"
#include "LeapBlueprintFunctionLibrary.h" // for AngleBetweenVectors()

#define PRINT_ONSCREEN_DEBUG (0 && WITH_EDITOR) 

float GetTime()
{
	return FPlatformTime::Seconds();
}
float SumFloatArray(const TArray<float>& ToSum)
{
	float Ret = 0;
	for (auto Value : ToSum)
	{
		Ret += Value;
	}
	return Ret;
}
float SumFloatArray(const TArray<float>& ToSum, const int NumElements)
{
	float Ret = 0;
	int Index = 0;
	for (auto Value : ToSum)
	{
		if (Index == NumElements)
		{
			break;
		}
		Ret += Value;
		
		Index++;
	}
	return Ret;
}
float Sum2DFloatArray(const TArray<TArray<float>>& ToSum, const int Index)
{
	float Ret = 0;
	if (ToSum.Num() < (Index + 1))
	{
		return 0;
	}
	for (auto jValue : ToSum[Index])
	{
		Ret += jValue;
	}
	return Ret;
}

FUltraleapCombinedDeviceConfidence::FUltraleapCombinedDeviceConfidence(IHandTrackingWrapper* LeapDeviceWrapperIn,
	ITrackingDeviceWrapper* TrackingDeviceWrapperIn,
	TArray<IHandTrackingWrapper*> DevicesToCombineIn)
	: FUltraleapCombinedDevice(LeapDeviceWrapperIn, TrackingDeviceWrapperIn, DevicesToCombineIn)
{
	// Maps need filling with defaults as they don't auto size with the array operator
	for (auto DeviceWrapper : DevicesToCombine)
	{
		LeftHandFirstVisible.Add(DeviceWrapper->GetDevice(), 0);
		RightHandFirstVisible.Add(DeviceWrapper->GetDevice(), 0);

		LastLeftHandPositions.Add(DeviceWrapper->GetDevice(), FHandPositionHistory());
		LastRightHandPositions.Add(DeviceWrapper->GetDevice(), FHandPositionHistory());

		JointConfidenceHistoriesLeft.Add(DeviceWrapper->GetDevice(), FJointConfidenceHistory(NumJointPositions));
		JointConfidenceHistoriesRight.Add(DeviceWrapper->GetDevice(), FJointConfidenceHistory(NumJointPositions));

		HandConfidenceHistoriesLeft.Add(DeviceWrapper->GetDevice(), FHandConfidenceHistory());
		HandConfidenceHistoriesRight.Add(DeviceWrapper->GetDevice(), FHandConfidenceHistory());
	}
	const int NumProviders = DevicesToCombine.Num();
	const int NumHandsPerProvider = 2;	  // until we evolve more
	
	// per hand
	JointConfidences.AddZeroed(NumProviders * NumHandsPerProvider);
	ConfidencesJointRot.AddZeroed(NumProviders * NumHandsPerProvider);
	ConfidencesJointPalmRot.AddZeroed(NumProviders * NumHandsPerProvider);
	ConfidencesJointOcclusion.AddZeroed(NumProviders * NumHandsPerProvider);

	for (int i = 0; i < (NumProviders * NumHandsPerProvider); ++i)
	{
		JointConfidences[i].AddZeroed(NumJointPositions);
		ConfidencesJointRot[i].AddZeroed(NumJointPositions);
		ConfidencesJointPalmRot[i].AddZeroed(NumJointPositions);
		ConfidencesJointOcclusion[i].AddZeroed(NumJointPositions);
	}
}
// if a joint occlusion actor is in the scene, this will get called in tick
// is the serial list/combined device matches this one
void FUltraleapCombinedDeviceConfidence::UpdateJointOcclusions(AJointOcclusionActor* Actor)
{
	if (!Actor || JointOcclusionFactor == 0)
	{
		return;
	}
	const auto& ColourCountMaps = Actor->GetColourCountMaps();
	
	for (auto Device : DevicesToCombine)
	{
		for (const auto Map : ColourCountMaps)
		{
			if (Map->DeviceSerial == Device->GetDeviceSerial())
			{

			}
		}
	}
}
void FUltraleapCombinedDeviceConfidence::CombineFrame(const TArray<FLeapFrameData>& SourceFrames)
{
	MergeFrames(SourceFrames, CurrentFrame);
}
// direct port from Unity
void FUltraleapCombinedDeviceConfidence::MergeFrames(const TArray<FLeapFrameData>& SourceFrames, FLeapFrameData& CombinedFrame )
{	
	TArray<const FLeapHandData*> LeftHands;
	TArray<const FLeapHandData*> RightHands;

	TArray<float> LeftHandConfidences;
	TArray<float> RightHandConfidences;

	TArray<TArray<float>> LeftJointConfidences;
	TArray<TArray<float>> RightJointConfidences;


	// make lists of all left and right hands found in each frame and also make a list of their confidences
	for (int FrameIdx = 0; FrameIdx < SourceFrames.Num(); FrameIdx++)
	{
		const FLeapFrameData& Frame = SourceFrames[FrameIdx];
	
		AddFrameToTimeVisibleDicts(SourceFrames, FrameIdx);

		for (const FLeapHandData& Hand : Frame.Hands)
		{
			if (Hand.HandType == EHandType::LEAP_HAND_LEFT)
			{
				LeftHands.Add(&Hand);

				float HandConfidence = CalculateHandConfidence(FrameIdx, Hand);
				TArray<float> JointConfidencesLocal;
				CalculateJointConfidence(FrameIdx, Hand, JointConfidencesLocal);

				LeftHandConfidences.Add(HandConfidence);
				LeftJointConfidences.Add(JointConfidencesLocal);
			}
			else
			{
				RightHands.Add(&Hand);

				float HandConfidence = CalculateHandConfidence(FrameIdx, Hand);
				TArray<float> JointConfidencesLocal;
				CalculateJointConfidence(FrameIdx, Hand, JointConfidencesLocal);

				RightHandConfidences.Add(HandConfidence);
				RightJointConfidences.Add(JointConfidencesLocal);
			}
		}
	}

	// normalize hand confidences:
	float Sum = SumFloatArray(LeftHandConfidences);
	
	if (Sum != 0)
	{
		for (int HandsIdx = 0; HandsIdx < LeftHandConfidences.Num(); HandsIdx++)
		{
			LeftHandConfidences[HandsIdx] /= Sum;
		}
	}
	else
	{
		for (int HandsIdx = 0; HandsIdx < LeftHandConfidences.Num(); HandsIdx++)
		{
			LeftHandConfidences[HandsIdx] = 1.0f / LeftHandConfidences.Num();
		}
	}
	Sum = SumFloatArray(RightHandConfidences);
	if (Sum != 0)
	{
		for (int HandsIdx = 0; HandsIdx < RightHandConfidences.Num(); HandsIdx++)
		{
			RightHandConfidences[HandsIdx] /= Sum;
		}
	}
	else
	{
		for (int HandsIdx = 0; HandsIdx < RightHandConfidences.Num(); HandsIdx++)
		{
			RightHandConfidences[HandsIdx] = 1.0f / RightHandConfidences.Num();
		}
	}

	// normalize joint confidences:
	for (int JointIdx = 0; JointIdx < NumJointPositions; JointIdx++)
	{
		Sum = Sum2DFloatArray(LeftJointConfidences, JointIdx);

		if (Sum != 0)
		{
			for (int HandsIdx = 0; HandsIdx < LeftJointConfidences.Num(); HandsIdx++)
			{
				LeftJointConfidences[HandsIdx][JointIdx] /= Sum;
			}
		}
		else
		{
			for (int HandsIdx = 0; HandsIdx < LeftJointConfidences.Num(); HandsIdx++)
			{
				LeftJointConfidences[HandsIdx][JointIdx] = 1.0f / LeftJointConfidences.Num();
			}
		}

		Sum = Sum2DFloatArray(RightJointConfidences, JointIdx);
		if (Sum != 0)
		{
			for (int HandsIdx = 0; HandsIdx < RightJointConfidences.Num(); HandsIdx++)
			{
				RightJointConfidences[HandsIdx][JointIdx] /= Sum;
			}
		}
		else
		{
			for (int HandsIdx = 0; HandsIdx < RightJointConfidences.Num(); HandsIdx++)
			{
				RightJointConfidences[HandsIdx][JointIdx] = 1.0f / RightJointConfidences.Num();
			}
		}
	}

	// combine hands using their confidences
	TArray<FLeapHandData> MergedHands;

	if (LeftHands.Num() > 0)
	{
		FLeapHandData Hand;
		
#if PRINT_ONSCREEN_DEBUG
		if (GEngine)
		{
			FString Message;
			Message = FString::Printf(TEXT("Num Left Hands %d"), LeftHands.Num());
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, Message);
		}
#endif //PRINT_ONSCREEN_DEBUG
		MergeHands(LeftHands, LeftHandConfidences, LeftJointConfidences, Hand);
		MergedHands.Add(Hand);
	}

	if (RightHands.Num() > 0)
	{
		FLeapHandData Hand;

#if PRINT_ONSCREEN_DEBUG
		if (GEngine)
		{
			FString Message;
			Message = FString::Printf(TEXT("Num Right Hands %d"), RightHands.Num());
			GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, Message);
		}
#endif
		MergeHands(RightHands, RightHandConfidences, RightJointConfidences, Hand);
		MergedHands.Add(Hand);
	}

	// create new frame and add merged hands to it
	// TODO: what about the other members of FLeapFrameData?
	CombinedFrame.Hands = MergedHands;
	CombinedFrame.NumberOfHandsVisible = MergedHands.Num();
}

/// create joint occlusion gameobjects if they are not there yet and update the position of all joint occlusion gameobjects that are
/// attached to a xr service provider
void FUltraleapCombinedDeviceConfidence::SetupJointOcclusion()
{
	if (JointOcclusions.Num() == 0)
	{
		for(auto Provider : DevicesToCombine)
		{
			//TODO: how to create these in the scene from C++?
			// Ideally pass them down from creation in Blueprint
			/* JointOcclusion JointOcclusion = provider.gameObject.GetComponentInChildren<JointOcclusion>();

			if (JointOcclusion == null)
			{
				JointOcclusion = GameObject.Instantiate(Resources.Load<GameObject>("JointOcclusionPrefab"), provider.transform)
									 .GetComponent<JointOcclusion>();

				foreach(CapsuleHand JointOcclusionHand in JointOcclusion.GetComponentsInChildren<CapsuleHand>(true))
				{
					JointOcclusionHand.leapProvider = Provider;
				}
			}

			jointOcclusions.Add(jointOcclusion);*/
		}

		for(JointOcclusion JointOcclusion : JointOcclusions)
		{
			//jointOcclusion.Setup();
		}
	}
	/*
	// if any providers are xr providers, update their jointOcclusions position and rotation
	for (int i = 0; i < JointOcclusions.Num(); i++)
	{
		LeapXRServiceProvider xrProvider = providers[i] as LeapXRServiceProvider;
		if (xrProvider != null)
		{
			Transform SourceDeviceOrigin = GetSourceDeviceOrigin(i);

			JointOcclusions[i].transform.SetPose(SourceDeviceOrigin.GetPose());
			JointOcclusions[i].transform.Rotate(new Vector3(-90, 0, 180));
		}
	}*/
}

/// add all hands in the frame given by frames[frameIdx] to the Dictionaries lastLeftHandPositions and lastRightHandPositions,
/// and update leftHandFirstVisible and rightHandFirstVisible
void FUltraleapCombinedDeviceConfidence::AddFrameToTimeVisibleDicts(const TArray<FLeapFrameData>& Frames,const int FrameIdx)
{
	bool HandsVisible[2] = {false};
	
	for(auto Hand : Frames[FrameIdx].Hands)
	{
		if (Hand.HandType == EHandType::LEAP_HAND_LEFT)
		{
			HandsVisible[0] = true;
			
			if (LeftHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] == 0)
			{
				LeftHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] = GetTime();
			}

			if (!LastLeftHandPositions.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
			{
				LastLeftHandPositions.Add(DevicesToCombine[FrameIdx]->GetDevice(), FHandPositionHistory());
			}
			
			
			LastLeftHandPositions[DevicesToCombine[FrameIdx]->GetDevice()].AddPosition(
				Hand.Palm.Position, GetTime());
		}
		else
		{
			HandsVisible[1] = true;
			if (RightHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] == 0)
			{
				RightHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] = GetTime();
			}

			if (!LastRightHandPositions.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
			{
				LastRightHandPositions.Add(DevicesToCombine[FrameIdx]->GetDevice(), FHandPositionHistory());
			}

			LastRightHandPositions[DevicesToCombine[FrameIdx]->GetDevice()].AddPosition(Hand.Palm.Position, GetTime());
		}
	}

	if (!HandsVisible[0])
	{
		LeftHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] =  0;
	}
	if (!HandsVisible[1])
	{
		RightHandFirstVisible[DevicesToCombine[FrameIdx]->GetDevice()] = 0;
	}
}
/// <summary>
/// combine different confidence functions to get an overall confidence for the given hand
/// uses frame_idx to find the corresponding provider that saw this hand
/// </summary>
float FUltraleapCombinedDeviceConfidence::CalculateHandConfidence(int FrameIdx,const FLeapHandData& Hand)
{
	float Confidence = 0;

	FTransform SourceDeviceOrigin = GetSourceDeviceOrigin(FrameIdx);

	Confidence = PalmPosFactor * ConfidenceRelativeHandPos(DevicesToCombine[FrameIdx]->GetDevice(), SourceDeviceOrigin, Hand.Palm.Position);
	Confidence += PalmRotFactor * ConfidenceRelativeHandRot(SourceDeviceOrigin, Hand.Palm.Position, Hand.Palm.Normal);
	
	Confidence += PalmVelocityFactor * ConfidenceRelativeHandVelocity(DevicesToCombine[FrameIdx]->GetDevice(), SourceDeviceOrigin,
										   Hand.Palm.Position, Hand.HandType == EHandType::LEAP_HAND_LEFT);

	// if ignoreRecentNewHands is true, then
	// the confidence should be 0 when it is the first frame with the hand in it.
	if (IgnoreRecentNewHands)
	{
		Confidence = Confidence * ConfidenceTimeSinceHandFirstVisible(
									  DevicesToCombine[FrameIdx]->GetDevice(), Hand.HandType == EHandType::LEAP_HAND_LEFT);
	}

	// average out new hand confidence with that of the last few frames
	if (Hand.HandType == EHandType::LEAP_HAND_LEFT)
	{
		if (!HandConfidenceHistoriesLeft.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
		{
			HandConfidenceHistoriesLeft.Add(DevicesToCombine[FrameIdx]->GetDevice(),FHandConfidenceHistory());
		}
		HandConfidenceHistoriesLeft[DevicesToCombine[FrameIdx]->GetDevice()].AddConfidence(Confidence);
		Confidence = HandConfidenceHistoriesLeft[DevicesToCombine[FrameIdx]->GetDevice()].GetAveragedConfidence();
	}
	else
	{
		if (!HandConfidenceHistoriesRight.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
		{
			HandConfidenceHistoriesRight.Add(DevicesToCombine[FrameIdx]->GetDevice(), FHandConfidenceHistory());
		}
		HandConfidenceHistoriesRight[DevicesToCombine[FrameIdx]->GetDevice()].AddConfidence(Confidence);
		Confidence = HandConfidenceHistoriesRight[DevicesToCombine[FrameIdx]->GetDevice()].GetAveragedConfidence();
	}

	return Confidence;
}

/// <summary>
/// uses the hand pos relative to the device to calculate a confidence.
/// using a 2d gauss with bigger spread when further away from the device
/// and amplitude depending on the ideal depth of the specific device and
/// the distance from hand to device
/// </summary>
float FUltraleapCombinedDeviceConfidence::ConfidenceRelativeHandPos(
	IHandTrackingDevice* Provider, const FTransform& SourceDeviceOrigin, const FVector& HandPos)
{
	// TODO could be inversetransform vector, was inversetransform point?
	FVector RelativeHandPos = SourceDeviceOrigin.InverseTransformPosition(HandPos);
	
	// 2d gauss

	// amplitude
	float a = 1 - RelativeHandPos.X;
	// pos of center of peak
	float x0 = 0;
	float y0 = 0;
	// spread
	float SigmaX = RelativeHandPos.X;
	float SigmaY = RelativeHandPos.X;

	// input pos
	float x = RelativeHandPos.Y;
	float y = RelativeHandPos.Z;

	//TODO: there was some logic here to decide if this was a provider/real device
	// presumable to allow you to chain aggregators together?
	// leaving as all IRL devices for the moment, with a special enum for combined LEAP_DEVICE_TYPE_COMBINED
	// if the frame is coming from a LeapServiceProvider, use different values depending on the type of device
	auto ProviderDeviceType = Provider->GetDeviceType();
	if (ProviderDeviceType == ELeapDeviceType::LEAP_DEVICE_TYPE_RIGEL ||
		ProviderDeviceType == ELeapDeviceType::LEAP_DEVICE_TYPE_SIR170 ||
		ProviderDeviceType == ELeapDeviceType::LEAP_DEVICE_TYPE_3DI)
	{
		// Depth: Between 10cm to 75cm preferred, up to 1m maximum
		// Field Of View: 170 x 170 degrees typical (160 x 160 degrees minimum)
		float CurrentDepth = RelativeHandPos.X;

		float RequiredWidth = (CurrentDepth / 2.0) / FMath::Sin(FMath::DegreesToRadians(170.0 / 2.0));
		SigmaX = 0.2f * RequiredWidth;
		SigmaY = 0.2f * RequiredWidth;

		// amplitude should be 1 within ideal depth and go 'smoothly' to zero on both sides of the ideal depth.
		if (CurrentDepth > 0.1f && CurrentDepth < 0.75)
		{
			a = 1.0f;
		}
		else if (CurrentDepth < 0.1f)
		{
			a = 0.55f / (PI / 2.0) * FMath::Atan(100 * (CurrentDepth + 0.05f)) + 0.5f;
		}
		else if (CurrentDepth > 0.75f)
		{
			a = -0.55f / (PI / 2.0) * FMath::Atan(50 * (CurrentDepth - 0.875f)) + 0.5f;
		}
	}
	else if (ProviderDeviceType == ELeapDeviceType::LEAP_DEVICE_TYPE_PERIPHERAL)
	{
		// Depth: Between 10cm to 60cm preferred, up to 80cm maximum
		// Field Of View: 140 x 120 degrees typical
		float CurrentDepth = RelativeHandPos.X;
		float RequiredWidthX = (CurrentDepth / 2) / FMath::Sin(FMath::DegreesToRadians(120.0 / 2.0));
		float RequiredWidthY = (CurrentDepth / 2) / FMath::Sin(FMath::DegreesToRadians(140.0 / 2.0));
		SigmaX = 0.2f * RequiredWidthX;
		SigmaY = 0.2f * RequiredWidthY;

		// amplitude should be 1 within ideal depth and go 'smoothly' to zero on both sides of the ideal depth.
		if (CurrentDepth > 0.1f && CurrentDepth < 0.6f)
		{
			a = 1.0f;
		}
		else if (CurrentDepth < 0.1f)
		{
			a = 0.55f / (PI / 2.0) * FMath::Atan(100 * (CurrentDepth + 0.05f)) + 0.5f;
		}
		else if (CurrentDepth > 0.6f)
		{
			a = -0.55f / (PI / 2.0) * FMath::Atan(50 * (CurrentDepth - 0.7f)) + 0.5f;
		}
	}

	float Confidence =
		a *
		FMath::Exp(-(FMath::Pow(x - x0, 2) / (2 * FMath::Pow(SigmaX, 2)) + FMath::Pow(y - y0, 2) / (2 * FMath::Pow(SigmaY, 2))));

	if (Confidence < 0)
		Confidence = 0;

	return Confidence;
}
/// <summary>
/// uses the palm normal relative to the direction from hand to device to calculate a confidence
/// </summary>
float FUltraleapCombinedDeviceConfidence::ConfidenceRelativeHandRot(
	const FTransform& SourceDeviceOrigin, const FVector& HandPos, const FVector& PalmNormal)
{
	// angle between palm normal and the direction from hand pos to device origin
	float PalmAngle = ULeapBlueprintFunctionLibrary::AngleBetweenVectors(PalmNormal, SourceDeviceOrigin.GetLocation() - HandPos);

	// get confidence based on a cos where it should be 1 if the angle is 0 or 180 degrees,
	// and it should be 0 if it is 90 degrees
	float Confidence = (FMath::Cos(FMath::DegreesToRadians( 2 * PalmAngle)) + 1.0f) / 2.0;

	return Confidence;
}

/// <summary>
/// uses the hand velocity to calculate a confidence.
/// returns a high confidence, if the velocity is low, and a low confidence otherwise.
/// Returns 0, if the hand hasn't been consistently tracked for about the last 10 frames
/// </summary>
float FUltraleapCombinedDeviceConfidence::ConfidenceRelativeHandVelocity(
	IHandTrackingDevice* Provider, const FTransform& SourceDeviceOrigin, const FVector HandPos, const bool IsLeft)
{
	FVector OldPosition;
	float OldTime;

	bool PositionsRecorded = IsLeft ? LastLeftHandPositions[Provider].GetOldestPosition(OldPosition, OldTime)
									: LastRightHandPositions[Provider].GetOldestPosition(OldPosition, OldTime);

	// if we haven't recorded any positions yet, or the hand hasn't been present in the last 10 frames (oldest position is older
	// than 10 * frame time), return 0
	if (!PositionsRecorded || (GetTime() - OldTime) > DeltaTimeFromTick * 10)
	{
		return 0;
	}

	float Velocity = FVector::Distance(HandPos, OldPosition) / (GetTime() - OldTime);

	float Confidence = 0;
	if (Velocity < 2)
	{
		Confidence = -0.5f * Velocity + 1;
	}

	return Confidence;
}

float FUltraleapCombinedDeviceConfidence::ConfidenceTimeSinceHandFirstVisible(IHandTrackingDevice* Provider, const bool IsLeft)
{
	if ((IsLeft ? LeftHandFirstVisible[Provider] : RightHandFirstVisible[Provider]) == 0)
	{
		return 0;
	}

	float LengthVisible = GetTime() - (IsLeft ? LeftHandFirstVisible[Provider] : RightHandFirstVisible[Provider]);

	float Confidence = 1;
	if (LengthVisible < 1)
	{
		Confidence = LengthVisible;
	}

	return Confidence;
}
/// <summary>
/// Combine different confidence functions to get an overall confidence for each joint in the given hand
/// uses frame_idx to find the corresponding provider that saw this hand
/// </summary>
void FUltraleapCombinedDeviceConfidence::CalculateJointConfidence(
	const int FrameIdx, const FLeapHandData& Hand, TArray<float>& RetConfidences)
{
	// get index in confidence arrays
	int idx = FrameIdx * 2 + (Hand.HandType == EHandType::LEAP_HAND_LEFT ? 0 : 1);
	const int NumProviders = DevicesToCombine.Num();
	
	FTransform SourceDeviceOrigin = GetSourceDeviceOrigin(FrameIdx);

	if (JointRotFactor != 0)
	{
		ConfidenceRelativeJointRot(ConfidencesJointRot[idx], SourceDeviceOrigin, Hand);
	}
	if (JointRotToPalmFactor != 0)
	{
		ConfidenceRelativeJointRotToPalmRot(ConfidencesJointPalmRot[idx], SourceDeviceOrigin, Hand);
	}
	if (JointOcclusionFactor != 0)
	{
		// todo joint occlusion component
	//	ConfidencesJointOcclusion[idx] =
	//		JointOcclusions[FrameIdx].ConfidenceJointOcclusion(ConfidencesJointOcclusion[idx], DeviceOrigin, Hand);
	}

	for (int FingerIdx = 0; FingerIdx < 5; FingerIdx++)
	{
		for (int BoneIdx = 0; BoneIdx < 5; BoneIdx++)
		{
			int key = FingerIdx * 5 + BoneIdx;
			JointConfidences[idx][key] = JointRotFactor * ConfidencesJointRot[idx][key] +
										 JointRotToPalmFactor * ConfidencesJointPalmRot[idx][key] +
										 JointOcclusionFactor * ConfidencesJointOcclusion[idx][key];

			if (BoneIdx != 0)
			{
				// average with the confidence from the last joint on the same finger,
				// so that outer joints jump around less.
				// eg. when a confidence is low on the knuckle of a finger, the finger tip confidence for the same finger
				// should take that into account and be slightly lower too
				JointConfidences[idx][key] += JointConfidences[idx][key - 1];
				JointConfidences[idx][key] /= 2;
			}
		}
	}

	// average out new joint confidence with that of the last few frames
	if (Hand.HandType == EHandType::LEAP_HAND_LEFT)
	{
		if (!JointConfidenceHistoriesLeft.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
		{
			JointConfidenceHistoriesLeft.Add(DevicesToCombine[FrameIdx]->GetDevice(), FJointConfidenceHistory(NumJointPositions));
		}
		JointConfidenceHistoriesLeft[DevicesToCombine[FrameIdx]->GetDevice()].AddConfidences(JointConfidences[idx]);
		JointConfidences[idx] = JointConfidenceHistoriesLeft[DevicesToCombine[FrameIdx]->GetDevice()].GetAveragedConfidences();
	}
	else
	{
		if (!JointConfidenceHistoriesRight.Contains(DevicesToCombine[FrameIdx]->GetDevice()))
		{
			JointConfidenceHistoriesRight.Add(DevicesToCombine[FrameIdx]->GetDevice(), FJointConfidenceHistory(NumJointPositions));
		}
		JointConfidenceHistoriesRight[DevicesToCombine[FrameIdx]->GetDevice()].AddConfidences(JointConfidences[idx]);
		JointConfidences[idx] = JointConfidenceHistoriesRight[DevicesToCombine[FrameIdx]->GetDevice()].GetAveragedConfidences();
	}

	RetConfidences = JointConfidences[idx];
}
/// <summary>
/// Merge hands based on hand confidences and joint confidences
/// </summary>
void FUltraleapCombinedDeviceConfidence::MergeHands(const TArray<const FLeapHandData*>& Hands,const TArray<float>& HandConfidences,const TArray<TArray<float>>& JointConfidencesIn, FLeapHandData& HandRet)
{
	bool IsLeft = (Hands[0]->HandType == EHandType::LEAP_HAND_LEFT);

	FVector MergedPalmPos = Hands[0]->Palm.Position * HandConfidences[0];
	FQuat MergedPalmRot = Hands[0]->Palm.Orientation.Quaternion();

	
	for (int HandsIdx = 1; HandsIdx < Hands.Num(); HandsIdx++)
	{
		// position
		MergedPalmPos += Hands[HandsIdx]->Palm.Position * HandConfidences[HandsIdx];

		// rotation
		float LerpValue = SumFloatArray(HandConfidences, HandsIdx) / SumFloatArray(HandConfidences,HandsIdx + 1);
		MergedPalmRot = FQuat::FastLerp(Hands[HandsIdx]->Palm.Orientation.Quaternion(), MergedPalmRot, LerpValue);
	}

	// joints
	TArray<FVector> MergedJointPositions;
	MergedJointPositions.AddZeroed(NumJointPositions);

	 TArray<TArray<FVector>> JointPositionsList;
	
	// in Unity, vector hand is used here to get the hand vectors in a
	// linear list which is in local space relative to palm
	for(auto Hand : Hands)
	{
		TArray<FVector> JointPositions;
		JointPositions.AddZeroed(NumJointPositions);
		CreateLocalLinearJointList(*Hand, JointPositions);
		// should be 25 vectors in here
		JointPositionsList.Add(JointPositions);
	}

	for (int HandsIdx = 0; HandsIdx < Hands.Num(); HandsIdx++)
	{
		for (int JointIdx = 0; JointIdx < NumJointPositions; JointIdx++)
		{
			MergedJointPositions[JointIdx] += JointPositionsList[HandsIdx][JointIdx] * (JointConfidencesIn[HandsIdx][JointIdx]);
		}
	}
	
	// combine everything to a hand
	ConvertToWorldSpaceHand(HandRet, IsLeft, MergedPalmPos, MergedPalmRot, MergedJointPositions);
	
	return;
}


/// <summary>
/// uses the normal vector of a joint / bone (outwards pointing one) and the direction from joint to device
/// to calculate per-joint confidence values
/// </summary>
void FUltraleapCombinedDeviceConfidence::ConfidenceRelativeJointRot(
	TArray<float>& Confidences, const FTransform& SourceDeviceOrigin, const FLeapHandData& Hand)
{
	if (Confidences.Num() == 0)
	{
		Confidences.AddZeroed(NumJointPositions);
	}
	static const int NumBones = 4;
	int FingerIndex = 0;
	for(auto Finger : Hand.Digits)
	{
		for (int BoneIdx = 0; BoneIdx < NumBones; BoneIdx++)
		{
			int Key = FingerIndex * NumBones + BoneIdx;

			FVector JointPos = Finger.Bones[BoneIdx].NextJoint;
			FVector JointNormalVector;
		
			if ( FingerIndex == 0)
			{
				JointNormalVector = Finger.Bones[BoneIdx].Rotation.Quaternion() * FVector::RightVector;
			}
			else
			{
				// Changed to Forward as X is Up in leap space
				JointNormalVector = Finger.Bones[BoneIdx].Rotation.Quaternion() * FVector::ForwardVector * -1;
			}

			float Angle =
				ULeapBlueprintFunctionLibrary::AngleBetweenVectors(JointPos - SourceDeviceOrigin.GetLocation(), JointNormalVector);

			// get confidence based on a cos where it should be 1 if the angle is 0 or 180 degrees,
			// and it should be 0 if it is 90 degrees
			Confidences[Key] = (FMath::Cos(FMath::DegreesToRadians(2 * Angle)) + 1.0f) / 2.0;
		}
		++FingerIndex;
	}
}

/// <summary>
/// uses the normal vector of a joint / bone (outwards pointing one) and the palm normal vector
/// to calculate per-joint confidence values
/// </summary>
void FUltraleapCombinedDeviceConfidence::ConfidenceRelativeJointRotToPalmRot(
	TArray<float>& Confidences, const FTransform& SourceDeviceOrigin, const FLeapHandData& Hand)
{
	if (Confidences.Num() == 0)
	{
		Confidences.AddZeroed(NumJointPositions);
	}

	for(auto Finger : Hand.Digits)
	{
		static const int NumBones = 4;
		int FingerIndex = 0;
		for (int BoneIdx = 0; BoneIdx < NumBones; BoneIdx++)
		{
			int Key = FingerIndex * NumBones + BoneIdx;

			FVector JointPos = Finger.Bones[BoneIdx].NextJoint;
			FVector JointNormalVector;

			JointNormalVector = Finger.Bones[BoneIdx].Rotation.Quaternion() * FVector::ForwardVector * -1;

			float Angle = ULeapBlueprintFunctionLibrary::AngleBetweenVectors(Hand.Palm.Normal, JointNormalVector);

			// get confidence based on a cos where it should be 1 if the angle is 0,
			// and it should be 0 if the angle is 180 degrees
			Confidences[Key] = (FMath::Cos(FMath::DegreesToRadians(Angle)) + 1.0f) / 2.0;
		}
		++FingerIndex;
	}

	return;
}
/// <summary>
/// return an array of joint confidences that is determined by joint occlusion.
/// It uses a capsule hand rendered on a camera sitting at the deviceOrigin.
/// Note that as the capsule hand doesn't have metacarpal bones, their corresponding confidence will be zero)
/// </summary>
void FUltraleapCombinedDeviceConfidence::StoreConfidenceJointOcclusion(AJointOcclusionActor* JointOcclusionActor, TArray<float>& Confidences,
	const FTransform& DeviceOriginIn, const FLeapHandData& Hand, IHandTrackingDevice* Provider)
{
	if (Confidences.Num() == 0)
	{
		Confidences.AddZeroed(NumJointPositions);
	}

	TArray<int> PixelsSeenCount;
	PixelsSeenCount.AddZeroed(Confidences.Num());

	TArray<int> OptimalPixelsCount;
	OptimalPixelsCount.AddZeroed(Confidences.Num());

	int FingerIndex = 0;
	for(auto& Finger : Hand.Digits)
	{
		static const int NumFingers = 5;
		static const int NumJoints = 4;
		for (int j = 0; j < NumJoints; j++)
		{
			// as the capsule hands doesn't render metacarpal bones, the indexing of capsule hand colors is different
			// from the indexing of the jointPositions on a VectorHand (which is used for confidence indexing)
			int Key = (int) FingerIndex * NumFingers + j + 1;
			
			// in unreal we render all bones?
			int CapsuleHandKey = Key;	 //(int) FingerIndex * NumJoints + j;

			OptimalPixelsCount[Key] = (int) (8);

			if (Hand.HandType == LEAP_HAND_LEFT)
			{
				FLinearColor TestColour = JointOcclusionActor->SphereColoursLeft[Key];
				// TODO: lookup colour map by device. and add pixel count per colour
			//	pixelsSeenCount[key] =
				//	tempPixels.Where(x = > DistanceBetweenColors(x, occlusionSphereColorsLeft[capsuleHandKey]) < 0.01f).Count();
			}
			else
			{
				FLinearColor TestColour = JointOcclusionActor->SphereColoursRight[Key];
				// TODO: lookup colour map by device. and add pixel count per colour
			//	pixelsSeenCount[key] =
				//	tempPixels.Where(x = > DistanceBetweenColors(x, occlusionSphereColorsRight[capsuleHandKey]) < 0.01f).Count();
			}
			FingerIndex++;
		}
	}

	for (int i = 0; i < Confidences.Num(); i++)
	{
		if (OptimalPixelsCount[i] != 0)
		{
			Confidences[i] = (float) PixelsSeenCount[i] / OptimalPixelsCount[i];
		}
	}
}
