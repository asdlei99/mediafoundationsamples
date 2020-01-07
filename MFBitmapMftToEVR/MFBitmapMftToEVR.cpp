/******************************************************************************
* Filename: MFBitmapMftToEVR.cpp
*
* Description:
* This file contains a C++ console application that is attempting to write raw
* bitmaps transformed with an Media Foundation color converter transform to the 
* Enhanced Video Renderer.
* (https://msdn.microsoft.com/en-us/library/windows/desktop/ms694916%28v=vs.85%29.aspx).
* This example extended the MFBitmapToEVR sample by adding the MFT transform.
*
* Status:
* Under development (unlikely to work properly or at all).
*
* Author:
* Aaron Clauson (aaron@sipsorcery.com)
*
* History:
* 07 Jan 2019	  Aaron Clauson	  Created, Hobart, Australia.
*
* License: Public Domain (no warranty, use at own risk)
/******************************************************************************/

#include "..\Common\MFUtility.h"

#include <d3d9.h>
#include <Dxva2api.h>
#include <evr.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>

#include <iostream>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")

#define BITMAP_WIDTH  640
#define BITMAP_HEIGHT 480
#define SAMPLE_DURATION 10000000 // 10^7 corresponds to 1 second.
#define SAMPLE_COUNT 10

// Forward function definitions.
DWORD InitializeWindow(LPVOID lpThreadParameter);

// Constants 
const WCHAR CLASS_NAME[] = L"MFBitmapToEVR Window Class";
const WCHAR WINDOW_NAME[] = L"MFBitmapToEVR";

// Globals.
HWND _hwnd;

int main()
{
  IMFMediaType* pVideoOutType = NULL;
  IMFMediaSink* pVideoSink = NULL;
  IMFStreamSink* pStreamSink = NULL;
  IMFMediaTypeHandler* pSinkMediaTypeHandler = NULL;
  IMFVideoRenderer* pVideoRenderer = NULL;
  IMFVideoDisplayControl* pVideoDisplayControl = NULL;
  IMFGetService* pService = NULL;
  IMFActivate* pActive = NULL;
  IMFPresentationClock* pClock = NULL;
  IMFPresentationTimeSource* pTimeSource = NULL;
  IDirect3DDeviceManager9* pD3DManager = NULL;
  IMFVideoSampleAllocator* pVideoSampleAllocator = NULL;
  IMFSample* pD3DVideoSample = NULL;
  RECT rc = { 0, 0, BITMAP_WIDTH, BITMAP_HEIGHT };
  BOOL fSelected = false;

  IUnknown* colorConvTransformUnk = NULL;
  IMFTransform* pColorConvTransform = NULL; // This is colour converter MFT is used to convert between RGB32 and RGB24.
  IMFMediaType* pDecInputMediaType = NULL, * pDecOutputMediaType = NULL;
  IMFMediaType* pVideoSourceOutType = NULL;

  CHECK_HR(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE),
    "COM initialisation failed.");

  CHECK_HR(MFStartup(MF_VERSION),
    "Media Foundation initialisation failed.");

  // Need the color converter DSP for conversions between YUV, RGB etc.
  CHECK_HR(MFTRegisterLocalByCLSID(
    __uuidof(CColorConvertDMO),
    MFT_CATEGORY_VIDEO_PROCESSOR,
    L"",
    MFT_ENUM_FLAG_SYNCMFT,
    0,
    NULL,
    0,
    NULL),
    "Error registering colour converter DSP.");

  // Create a separate Window and thread to host the Video player.
  CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)InitializeWindow, NULL, 0, NULL);
  Sleep(1000);
  if (_hwnd == nullptr)
  {
    printf("Failed to initialise video window.\n");
    goto done;
  }

  if (_hwnd == nullptr)
  {
    printf("Failed to initialise video window.\n");
    goto done;
  }

  // ----- Set up Video sink (Enhanced Video Renderer). -----

  CHECK_HR(MFCreateVideoRendererActivate(_hwnd, &pActive),
    "Failed to created video rendered activation context.");

  CHECK_HR(pActive->ActivateObject(IID_IMFMediaSink, (void**)&pVideoSink),
    "Failed to activate IMFMediaSink interface on video sink.");

  // Initialize the renderer before doing anything else including querying for other interfaces,
  // see https://msdn.microsoft.com/en-us/library/windows/desktop/ms704667(v=vs.85).aspx.
  CHECK_HR(pVideoSink->QueryInterface(__uuidof(IMFVideoRenderer), (void**)&pVideoRenderer),
    "Failed to get video Renderer interface from EVR media sink.");

  CHECK_HR(pVideoRenderer->InitializeRenderer(NULL, NULL),
    "Failed to initialise the video renderer.");

  CHECK_HR(pVideoSink->QueryInterface(__uuidof(IMFGetService), (void**)&pService),
    "Failed to get service interface from EVR media sink.");

  CHECK_HR(pService->GetService(MR_VIDEO_RENDER_SERVICE, __uuidof(IMFVideoDisplayControl), (void**)&pVideoDisplayControl),
    "Failed to get video display control interface from service interface.");

  CHECK_HR(pVideoDisplayControl->SetVideoWindow(_hwnd),
    "Failed to SetVideoWindow.");

  CHECK_HR(pVideoDisplayControl->SetVideoPosition(NULL, &rc),
    "Failed to SetVideoPosition.");

  CHECK_HR(pVideoSink->GetStreamSinkByIndex(0, &pStreamSink),
    "Failed to get video renderer stream by index.");

  CHECK_HR(pStreamSink->GetMediaTypeHandler(&pSinkMediaTypeHandler),
    "Failed to get media type handler for stream sink.");

  DWORD sinkMediaTypeCount = 0;
  CHECK_HR(pSinkMediaTypeHandler->GetMediaTypeCount(&sinkMediaTypeCount),
    "Failed to get sink media type count.");

  std::cout << "Sink media type count: " << sinkMediaTypeCount << "." << std::endl;

  // ----- Create a compatible media type and set on the source and sink. -----

  // Set the video input type on the EVR sink.
  CHECK_HR(MFCreateMediaType(&pVideoOutType), "Failed to create video output media type.");
  CHECK_HR(pVideoOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "Failed to set video output media major type.");
  CHECK_HR(pVideoOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32), "Failed to set video sub-type attribute on media type.");
  CHECK_HR(pVideoOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "Failed to set interlace mode attribute on media type.");
  CHECK_HR(pVideoOutType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE), "Failed to set independent samples attribute on media type.");
  CHECK_HR(MFSetAttributeRatio(pVideoOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "Failed to set pixel aspect ratio attribute on media type.");
  CHECK_HR(MFSetAttributeSize(pVideoOutType, MF_MT_FRAME_SIZE, BITMAP_WIDTH, BITMAP_HEIGHT), "Failed to set the frame size attribute on media type.");

  /* CHECK_HR(CoCreateInstance(CLSID_CColorConvertDMO, NULL, CLSCTX_INPROC_SERVER,
     IID_IUnknown, (void**)&colorConvTransformUnk),
     "Failed to create colour converter MFT.");

   CHECK_HR(colorConvTransformUnk->QueryInterface(IID_PPV_ARGS(&pColorConvTransform)),
     "Failed to get IMFTransform interface from colour converter MFT object.");

   MFCreateMediaType(&pDecInputMediaType);
   CHECK_HR(pVideoSourceOutType->CopyAllItems(pDecInputMediaType), "Error copying media type attributes to colour converter input media type.");
   CHECK_HR(pColorConvTransform->SetInputType(0, pDecInputMediaType, 0), "Failed to set input media type on colour converter MFT.");

   MFCreateMediaType(&pDecOutputMediaType);
   CHECK_HR(pVideoOutType->CopyAllItems(pDecOutputMediaType), "Error copying media type attributes to colour converter output media type.");
   CHECK_HR(pColorConvTransform->SetOutputType(0, pDecOutputMediaType, 0), "Failed to set output media type on colour converter MFT.");

   DWORD mftStatus = 0;
   CHECK_HR(pColorConvTransform->GetInputStatus(0, &mftStatus), "Failed to get input status from colour converter MFT.");
   if (MFT_INPUT_STATUS_ACCEPT_DATA != mftStatus) {
     printf("Colour converter MFT is not accepting data.\n");
     goto done;
   }

   CHECK_HR(pColorConvTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL), "Failed to process FLUSH command on colour converter MFT.");
   CHECK_HR(pColorConvTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL), "Failed to process BEGIN_STREAMING command on colour converter MFT.");
   CHECK_HR(pColorConvTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL), "Failed to process START_OF_STREAM command on colour converter MFT.");*/

   //CHECK_HR(GetSupportedMediaType(pMediaTypeHandler, &pVideoOutType),
   //  "Failed to get supported media type.");

  std::cout << "EVR input media type defined as:" << std::endl;
  std::cout << GetMediaTypeDescription(pVideoOutType) << std::endl << std::endl;

  CHECK_HR(pSinkMediaTypeHandler->SetCurrentMediaType(pVideoOutType),
    "Failed to set input media type on EVR sink.");

  // ----- Source and sink now configured. Set up remaining infrastructure and then start sampling. -----

  // Get Direct3D surface organised.
  // https://msdn.microsoft.com/fr-fr/library/windows/desktop/aa473823(v=vs.85).aspx
  CHECK_HR(MFGetService(pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pVideoSampleAllocator)), "Failed to get IMFVideoSampleAllocator.");
  CHECK_HR(MFGetService(pVideoSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pD3DManager)), "Failed to get Direct3D manager from EVR media sink.");
  CHECK_HR(pVideoSampleAllocator->SetDirectXManager(pD3DManager), "Failed to set D3DManager on video sample allocator.");
  CHECK_HR(pVideoSampleAllocator->InitializeSampleAllocator(1, pVideoOutType), "Failed to initialise video sample allocator.");
  CHECK_HR(pVideoSampleAllocator->AllocateSample(&pD3DVideoSample), "Failed to allocate video sample.");

  // Get clocks organised.
  CHECK_HR(MFCreatePresentationClock(&pClock), "Failed to create presentation clock.");
  CHECK_HR(MFCreateSystemTimeSource(&pTimeSource), "Failed to create system time source.");
  CHECK_HR(pClock->SetTimeSource(pTimeSource), "Failed to set time source.");
  CHECK_HR(pVideoSink->SetPresentationClock(pClock), "Failed to set presentation clock on video sink.");
  CHECK_HR(pClock->Start(0), "Error starting presentation clock.");

  // Start the sample read-write loop.
  IMFMediaBuffer* pDstBuffer = NULL;
  IMF2DBuffer* p2DBuffer = NULL;
  BYTE* bitmapBuffer = new BYTE[4 * BITMAP_WIDTH * BITMAP_HEIGHT]; // RGB32
  DWORD bitmapBufferLength = 4 * BITMAP_WIDTH * BITMAP_HEIGHT;
  LONGLONG llTimeStamp = 0;
  UINT bitmapCount = 0;
  LONGLONG sampleDuration = SAMPLE_DURATION;

  while (bitmapCount < SAMPLE_COUNT)
  {
    printf("Attempting to write bitmap to EVR, sample count %d, sample duration %llu, sample time %llu.\n", bitmapCount, sampleDuration, llTimeStamp);

    if (bitmapCount % 2 == 0) {
      for (int i = 0; i < bitmapBufferLength; i += 4) {
        bitmapBuffer[i + 0] = 0x00;
        bitmapBuffer[i + 1] = 0xff;
        bitmapBuffer[i + 2] = 0x00;
        bitmapBuffer[i + 3] = 0x00;
      }
    }
    else {
      for (int i = 0; i < bitmapBufferLength; i += 4) {
        bitmapBuffer[i + 0] = 0xff;
        bitmapBuffer[i + 1] = 0x00;
        bitmapBuffer[i + 2] = 0x00;
        bitmapBuffer[i + 3] = 0x00;
      }
    }

    CHECK_HR(pD3DVideoSample->SetSampleTime(llTimeStamp), "Failed to set D3D video sample time.");
    CHECK_HR(pD3DVideoSample->SetSampleDuration(sampleDuration), "Failed to set D3D video sample duration.");
    CHECK_HR(pD3DVideoSample->GetBufferByIndex(0, &pDstBuffer), "Failed to get destination buffer.");
    CHECK_HR(pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer)), "Failed to get pointer to 2D buffer.");
    CHECK_HR(p2DBuffer->ContiguousCopyFrom(bitmapBuffer, bitmapBufferLength), "Failed to copy bitmap to D2D buffer.");

    CHECK_HR(pStreamSink->ProcessSample(pD3DVideoSample), "Streamsink process sample failed.");

    Sleep(SAMPLE_DURATION / 10000);

    bitmapCount++;
    llTimeStamp += sampleDuration;
  }

  delete bitmapBuffer;
  SAFE_RELEASE(p2DBuffer);
  SAFE_RELEASE(pDstBuffer);

done:

  printf("finished.\n");
  auto c = getchar();

  SAFE_RELEASE(pVideoOutType);
  SAFE_RELEASE(pVideoSink);
  SAFE_RELEASE(pStreamSink);
  SAFE_RELEASE(pSinkMediaTypeHandler);
  SAFE_RELEASE(pVideoRenderer);
  SAFE_RELEASE(pVideoDisplayControl);
  SAFE_RELEASE(pService);
  SAFE_RELEASE(pActive);
  SAFE_RELEASE(pClock);
  SAFE_RELEASE(pTimeSource);
  SAFE_RELEASE(pD3DManager);
  SAFE_RELEASE(pVideoSampleAllocator);
  SAFE_RELEASE(pD3DVideoSample);

  return 0;
}

/**
* Initialises a new empty Window to host the video renderer and
* starts the message loop. This function needs to be called on a
* separate thread as it does not return until the Window is closed.
*/
DWORD InitializeWindow(LPVOID lpThreadParameter)
{
  WNDCLASS wc = { 0 };

  wc.lpfnWndProc = DefWindowProc;
  wc.hInstance = GetModuleHandle(NULL);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.lpszClassName = CLASS_NAME;

  if (RegisterClass(&wc))
  {
    _hwnd = CreateWindow(
      CLASS_NAME,
      WINDOW_NAME,
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      BITMAP_WIDTH,
      BITMAP_HEIGHT,
      NULL,
      NULL,
      GetModuleHandle(NULL),
      NULL
    );

    if (_hwnd)
    {
      ShowWindow(_hwnd, SW_SHOWDEFAULT);
      MSG msg = { 0 };

      while (true)
      {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
        else
        {
          Sleep(1);
        }
      }
    }
  }

  return 0;
}
