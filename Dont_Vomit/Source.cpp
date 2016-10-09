#define _CRT_SECURE_NO_WARNINGS
//#pragma warning(disable : 4244)
#pragma warning(disable : 4305)

// Include DirectX
#include "Oculus_Dx_Render.h"

// Include the Oculus SDK
#include "OVR_CAPI_D3D.h"

#include "Source.h"

//------------------------------------------------------------
// ovrSwapTextureSet wrapper class that also maintains the render target views
// needed for D3D11 rendering.
struct OculusTexture
{
	ovrSession               Session;
	ovrTextureSwapChain      TextureChain;
	std::vector<ID3D11RenderTargetView*> TexRtv;

	OculusTexture() :
		Session(nullptr),
		TextureChain(nullptr)
	{
	}

	bool Init(ovrSession session, int sizeW, int sizeH)
	{
		Session = session;

		ovrTextureSwapChainDesc desc = {};
		desc.Type = ovrTexture_2D;
		desc.ArraySize = 1;
		desc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.Width = sizeW;
		desc.Height = sizeH;
		desc.MipLevels = 1;
		desc.SampleCount = 1;
		desc.MiscFlags = ovrTextureMisc_DX_Typeless;
		desc.BindFlags = ovrTextureBind_DX_RenderTarget;
		desc.StaticImage = ovrFalse;

		ovrResult result = ovr_CreateTextureSwapChainDX(session, DIRECTX.Device, &desc, &TextureChain);
		if (!OVR_SUCCESS(result))
			return false;

		int textureCount = 0;
		ovr_GetTextureSwapChainLength(Session, TextureChain, &textureCount);
		for (int i = 0; i < textureCount; ++i)
		{
			ID3D11Texture2D* tex = nullptr;
			ovr_GetTextureSwapChainBufferDX(Session, TextureChain, i, IID_PPV_ARGS(&tex));
			D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
			rtvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			ID3D11RenderTargetView* rtv;
			DIRECTX.Device->CreateRenderTargetView(tex, &rtvd, &rtv);
			TexRtv.push_back(rtv);
			tex->Release();
		}

		return true;
	}

	~OculusTexture()
	{
		for (int i = 0; i < (int)TexRtv.size(); ++i)
		{
			Release(TexRtv[i]);
		}
		if (TextureChain)
		{
			ovr_DestroyTextureSwapChain(Session, TextureChain);
		}
	}

	ID3D11RenderTargetView* GetRTV()
	{
		int index = 0;
		ovr_GetTextureSwapChainCurrentIndex(Session, TextureChain, &index);
		return TexRtv[index];
	}

	// Commit changes
	void Commit()
	{
		ovr_CommitTextureSwapChain(Session, TextureChain);
	}
};

Scene          * roomScene = nullptr;
Camera         * mainCam = nullptr;

OculusTexture  * pEyeRenderTexture[2] = { nullptr, nullptr };
DepthBuffer    * pEyeDepthBuffer[2] = { nullptr, nullptr };
ovrRecti         eyeRenderViewport[2];

ovrMirrorTexture mirrorTexture = nullptr;

long long frameIndex = STARTTIME;
bool isVisible = true;

ovrSession session;
ovrHmdDesc hmdDesc;

static bool SetupMainLoop(){
	// Initialize these to nullptr here to handle device lost failures cleanly
	ovrMirrorTextureDesc mirrorDesc = {};

	ovrGraphicsLuid luid;
	ovrResult result = ovr_Create(&session, &luid);
	if (!OVR_SUCCESS(result)){
		return false;
	}

	hmdDesc = ovr_GetHmdDesc(session);

	// Setup Device and Graphics
	// Note: the mirror window can be any size, for this sample we use 1/2 the HMD resolution
	if (!DIRECTX.InitDevice(hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2, reinterpret_cast<LUID*>(&luid))){
		return false;
	}

	// Make the eye render buffers (caution if actual size < requested due to HW limits). 
	for (int eye = 0; eye < 2; ++eye)
	{
		ovrSizei idealSize = ovr_GetFovTextureSize(session, (ovrEyeType)eye, hmdDesc.DefaultEyeFov[eye], 1.0f);
		pEyeRenderTexture[eye] = new OculusTexture();
		if (!pEyeRenderTexture[eye]->Init(session, idealSize.w, idealSize.h))
		{
			return false;
		}
		pEyeDepthBuffer[eye] = new DepthBuffer(DIRECTX.Device, idealSize.w, idealSize.h);
		eyeRenderViewport[eye].Pos.x = 0;
		eyeRenderViewport[eye].Pos.y = 0;
		eyeRenderViewport[eye].Size = idealSize;
		if (!pEyeRenderTexture[eye]->TextureChain)
		{
			return false;
		}
	}

	// Create a mirror to see on the monitor.
	mirrorDesc.Format = OVR_FORMAT_R8G8B8A8_UNORM_SRGB;
	mirrorDesc.Width = DIRECTX.WinSizeW;
	mirrorDesc.Height = DIRECTX.WinSizeH;
	result = ovr_CreateMirrorTextureDX(session, DIRECTX.Device, &mirrorDesc, &mirrorTexture);
	if (!OVR_SUCCESS(result))
	{
		return false;
	}

	// Create the room model
	roomScene = new Scene();

	// Create camera
	mainCam = new Camera(&XMVectorSet(0.0f, 0.0f, 5.0f, 0), &XMQuaternionIdentity());

	// FloorLevel will give tracking poses where the floor height is 0
	ovr_SetTrackingOriginType(session, ovrTrackingOrigin_FloorLevel);

	return true;
}

#define CHECK_TIMING(i) (((float)(frameIndex / FPS) > timings[i]) && ((float)(frameIndex / FPS) < (timings[i+1]+timings[i])))

ovrVector3f      IPD_Persistance_copy[2];
static float axes[3] = { 0, 0, 0 };//roll, pitch, yaw

// return false to quit 
static bool MainLoop()
{
	//***Handle movement***

	//constant acceleration change
	XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -0.05f, 0), mainCam->Rot);
	XMVECTOR backward = XMVector3Rotate(XMVectorSet(0, 0, 0.05f, 0), mainCam->Rot);
	XMVECTOR right = XMVector3Rotate(XMVectorSet(0.05f, 0, 0, 0), mainCam->Rot);
	XMVECTOR left = XMVector3Rotate(XMVectorSet(-0.05f, 0, 0, 0), mainCam->Rot);

	//get controller state and movemenet
	XINPUT_STATE controller_state;
	DWORD dwResult = XInputGetState(0, &controller_state);
	if (controller_state.Gamepad.sThumbLY > YDEADZONE || DIRECTX.Key['W'] || DIRECTX.Key[VK_UP]){
		if (mainCam->Pos.m128_f32[2]>-19.9)
			mainCam->Pos = XMVectorAdd(mainCam->Pos, forward);
	}
	if (controller_state.Gamepad.sThumbLY < -YDEADZONE || DIRECTX.Key['S'] || DIRECTX.Key[VK_DOWN]){
		if (mainCam->Pos.m128_f32[2]<19.9)
			mainCam->Pos = XMVectorAdd(mainCam->Pos, backward);
	}
	if (controller_state.Gamepad.sThumbLX < -XDEADZONE || DIRECTX.Key['A']){
		if (mainCam->Pos.m128_f32[0]>-9.9)
			mainCam->Pos = XMVectorAdd(mainCam->Pos, left);
	}
	if (controller_state.Gamepad.sThumbLX > XDEADZONE || DIRECTX.Key['D']){
		if (mainCam->Pos.m128_f32[0]<9.9)
			mainCam->Pos = XMVectorAdd(mainCam->Pos, right);
	}
	if (controller_state.Gamepad.wButtons > 0){
		printf("Time %f\n", frameIndex / FPS);
	}

	//**Get dynamic rift settings**

	// Call ovr_GetRenderDesc each frame to get the ovrEyeRenderDesc, as the returned values (e.g. HmdToEyeOffset) may change at runtime.
	ovrEyeRenderDesc eyeRenderDesc[2];
	eyeRenderDesc[0] = ovr_GetRenderDesc(session, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
	eyeRenderDesc[1] = ovr_GetRenderDesc(session, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

	// Get both eye poses simultaneously, with IPD offset already included. 
	ovrPosef         EyeRenderPose[2];
	ovrVector3f      HmdToEyeOffset[2] = { eyeRenderDesc[0].HmdToEyeOffset,
										   eyeRenderDesc[1].HmdToEyeOffset };

	//**Perform the unplesantness based on running time

	//flicker every few frames
	if (CHECK_TIMING(0)){
#ifdef DEBUGGING
		printf("flickering ");
#endif
		if (frameIndex%flicker_frames == 0){
			for (int i = 0; i < roomScene->numModels; i++){
				uint32_t color = ((rand() & 0xff) << 24) | ((rand() & 0xff) << 16) | ((rand() & 0xff) << 8) | ((rand() & 0xff) << 0);
				roomScene->Models[i]->Fill->Tex->AutoFillTexture(7, color);
			}
		}
	}
	//increase flickr amount
	if (CHECK_TIMING(2)){
		flicker_frames = 3;
	}
	//slowly ajust IPD normal->wide
	if (CHECK_TIMING(4)){
#ifdef DEBUGGING
		printf("IPD1 ");
#endif
		float percent = (frameIndex - FPS * timings[4]) / (FPS * timings[5]);
		HmdToEyeOffset[0].x -= (float)0.65*percent;
		HmdToEyeOffset[1].x += (float)0.65*percent;
	}
	//more quickly ajust IPD wide--->normal--->small
	if (CHECK_TIMING(6)){
#ifdef DEBUGGING
		printf("IPD2 ");
#endif
		float percent = (frameIndex - FPS * timings[6]) / (FPS * timings[7]);
		HmdToEyeOffset[0].x = (HmdToEyeOffset[0].x-0.65) + 1.5*percent;
		HmdToEyeOffset[1].x = (HmdToEyeOffset[1].x+0.65) - 1.5*percent;
	}
	//quickly and randomly ajust IDP every few frames
	if (CHECK_TIMING(8)){
#ifdef DEBUGGING
		printf("IPD_rand ");
#endif
		if (frameIndex % 13 == 0){
			HmdToEyeOffset[0].x = (float)((rand() / (float)RAND_MAX) - 0.5);
			HmdToEyeOffset[0].y = (float)(((rand()*0.75) / (float)RAND_MAX) - 0.375);
			HmdToEyeOffset[1].x = (float)((rand() / (float)RAND_MAX) - 0.5);
			HmdToEyeOffset[1].y = (float)(((rand()*0.75) / (float)RAND_MAX) - 0.375);
			memcpy(IPD_Persistance_copy, HmdToEyeOffset, sizeof(HmdToEyeOffset));
		}
		else{
			memcpy(HmdToEyeOffset,IPD_Persistance_copy, sizeof(HmdToEyeOffset));
		}
	}
	//cross eyes
	if (CHECK_TIMING(10)){
#ifdef DEBUGGING
		printf("IPD_cross ");
#endif
		float ipd_0_x = HmdToEyeOffset[0].x;
		HmdToEyeOffset[0].x = HmdToEyeOffset[1].x+0.5;
		HmdToEyeOffset[1].x = ipd_0_x-0.5;
	}
	//slowly rotate yaw and pitch
	if (CHECK_TIMING(12) && frameIndex % 6 == 0){
#ifdef DEBUGGING
		printf("roll_pitch_yaw ");
#endif
		axes[2] += 0.003;
		axes[1] += 0.003;
		axes[0] += 0.003;
		mainCam->Rot = XMQuaternionRotationRollPitchYaw(axes[1], axes[2], axes[0]);
	}


#ifdef DEBUGGING
	printf("%d\n", frameIndex);
	printf("%f %f\n", HmdToEyeOffset[0].x, HmdToEyeOffset[1].x);
#endif

	//**Render Scene**

	double sensorSampleTime;    // sensorSampleTime is fed into the layer later
	ovr_GetEyePoses(session, frameIndex, ovrTrue, HmdToEyeOffset, EyeRenderPose, &sensorSampleTime);

	if (isVisible)
	{
		for (int eye = 0; eye < 2; ++eye)
		{
			// Clear and set up rendertarget
			DIRECTX.SetAndClearRenderTarget(pEyeRenderTexture[eye]->GetRTV(), pEyeDepthBuffer[eye]);
			DIRECTX.SetViewport((float)eyeRenderViewport[eye].Pos.x, (float)eyeRenderViewport[eye].Pos.y,
				(float)eyeRenderViewport[eye].Size.w, (float)eyeRenderViewport[eye].Size.h);

			//Get the pose information in XM format
			XMVECTOR eyeQuat = XMVectorSet(EyeRenderPose[eye].Orientation.x, EyeRenderPose[eye].Orientation.y,
				EyeRenderPose[eye].Orientation.z, EyeRenderPose[eye].Orientation.w);
			XMVECTOR eyePos = XMVectorSet(EyeRenderPose[eye].Position.x, EyeRenderPose[eye].Position.y, EyeRenderPose[eye].Position.z, 0);

			// Get view and projection matrices for the Rift camera
			XMVECTOR CombinedPos = XMVectorAdd(mainCam->Pos, XMVector3Rotate(eyePos, mainCam->Rot));
			Camera finalCam(&CombinedPos, &(XMQuaternionMultiply(eyeQuat, mainCam->Rot)));
			XMMATRIX view = finalCam.GetViewMatrix();
			ovrMatrix4f p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f, ovrProjection_None);
			XMMATRIX proj = XMMatrixSet(p.M[0][0], p.M[1][0], p.M[2][0], p.M[3][0],
				p.M[0][1], p.M[1][1], p.M[2][1], p.M[3][1],
				p.M[0][2], p.M[1][2], p.M[2][2], p.M[3][2],
				p.M[0][3], p.M[1][3], p.M[2][3], p.M[3][3]);
			XMMATRIX prod = XMMatrixMultiply(view, proj);
			roomScene->Render(&prod, 1, 1, 1, 1, true);

			// Commit rendering to the swap chain
			pEyeRenderTexture[eye]->Commit();
		}
		frameIndex++;
	}


	//**Finalize render settings and submit

	// Initialize our single full screen Fov layer.
	ovrLayerEyeFov ld = {};
	ld.Header.Type = ovrLayerType_EyeFov;
	ld.Header.Flags = 0;

	for (int eye = 0; eye < 2; ++eye)
	{
		ld.ColorTexture[eye] = pEyeRenderTexture[eye]->TextureChain;
		ld.Viewport[eye] = eyeRenderViewport[eye];
		ld.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
		ld.RenderPose[eye] = EyeRenderPose[eye];
		ld.SensorSampleTime = sensorSampleTime;
	}

	ovrLayerHeader* layers = &ld.Header;
	ovrResult result = ovr_SubmitFrame(session, frameIndex, nullptr, &layers, 1);
	// exit the rendering loop if submit returns an error, will retry on ovrError_DisplayLost
	if (!OVR_SUCCESS(result)){
		return result == ovrError_DisplayLost;
	}

	isVisible = (result == ovrSuccess);

	ovrSessionStatus sessionStatus;
	ovr_GetSessionStatus(session, &sessionStatus);
	if (sessionStatus.ShouldQuit)
		return false;
	if (sessionStatus.ShouldRecenter)
		ovr_RecenterTrackingOrigin(session);

	// Render mirror
	ID3D11Texture2D* tex = nullptr;
	ovr_GetMirrorTextureBufferDX(session, mirrorTexture, IID_PPV_ARGS(&tex));
	DIRECTX.Context->CopyResource(DIRECTX.BackBuffer, tex);
	tex->Release();
	DIRECTX.SwapChain->Present(0, 0);

	return true;
}

static void ExitMainLoop(){
	// Release resources
	delete mainCam;
	delete roomScene;
	if (mirrorTexture)
		ovr_DestroyMirrorTexture(session, mirrorTexture);
	for (int eye = 0; eye < 2; ++eye)
	{
		delete pEyeRenderTexture[eye];
		delete pEyeDepthBuffer[eye];
	}
	DIRECTX.ReleaseDevice();
	ovr_Destroy(session);
}

//-------------------------------------------------------------------------------------
int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int)
{
	// Allocate a console for this app
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	freopen("CON", "w", stdout);

	// Initializes LibOVR, and the Rift
	ovrResult result = ovr_Initialize(nullptr);
	VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

	VALIDATE(DIRECTX.InitWindow(hinst, L"Don't Vomit"), "Failed to open window.");

	//DIRECTX.Run(MainLoop);
	bool resultLoop = SetupMainLoop();
	// Main loop
	while (DIRECTX.HandleMessages() && resultLoop)
	{
		resultLoop = MainLoop();
	}
	ExitMainLoop();

	ovr_Shutdown();
	return(0);
}
