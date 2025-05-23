/*
    Ruby Licence
    Copyright (c) 2025 Petru Soroaga petrusoroaga@yahoo.com
    All rights reserved.

    Redistribution and/or use in source and/or binary forms, with or without
    modification, are permitted provided that the following conditions are met:
        * Redistributions and/or use of the source code (partially or complete) must retain
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Redistributions in binary form (partially or complete) must reproduce
        the above copyright notice, this list of conditions and the following disclaimer
        in the documentation and/or other materials provided with the distribution.
        * Copyright info and developer info must be preserved as is in the user
        interface, additions could be made to that info.
        * Neither the name of the organization nor the
        names of its contributors may be used to endorse or promote products
        derived from this software without specific prior written permission.
        * Military use is not permitted.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE AUTHOR (PETRU SOROAGA) BE LIABLE FOR ANY
    DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
    LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
    ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
    SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include "../base/base.h"
#include "../base/config.h"
#include "../base/models.h"
#include "../base/utils.h"
#include "../base/hardware_cam_maj.h"
#include "../common/string_utils.h"
#include "adaptive_video.h"
#include "shared_vars.h"
#include "timers.h"
#include "video_tx_buffers.h"
#include "video_source_csi.h"
#include "video_source_majestic.h"
#include "test_link_params.h"
#include "packets_utils.h"
#include "negociate_radio.h"

u8 s_uLastVideoProfileRequestedByController = 0xFF;
u32 s_uTimeLastVideoProfileRequestedByController = 0;
u32 s_uTimeLastTimeAdaptivePeriodicLoop = 0;

u16 s_uCurrentKFValue = 0;
u16 s_uPendingKFValue = 0;

int s_iPendingAdaptiveRadioDataRate = 0;
u32 s_uTimeSetPendingAdaptiveRadioDataRate = 0;

// Set inital state to uplink lost, to use lower video scheme, untill pairing comes from controller
bool s_bAdaptiveIsInUplinkLostState = true;
u32  s_uAdaptiveVideoTemporaryBitrate = 0;
u32  s_uAdaptiveTimeToSetInitialBitrate = 0;

void adaptive_video_init()
{
   log_line("[AdaptiveVideo] Init...");
   s_uPendingKFValue = 0;
   s_uCurrentKFValue = g_pCurrentModel->getInitialKeyframeIntervalMs(g_pCurrentModel->video_params.user_selected_video_link_profile);
   s_uTimeLastTimeAdaptivePeriodicLoop = get_current_timestamp_ms();

   s_iPendingAdaptiveRadioDataRate = 0;
   s_uTimeSetPendingAdaptiveRadioDataRate = 0;

   s_uLastVideoProfileRequestedByController = 0xFF;
   s_uTimeLastVideoProfileRequestedByController = 0;
   s_uAdaptiveTimeToSetInitialBitrate = g_TimeNow + 2000;
   log_line("[AdaptiveVideo] Done init. Current KF ms: %d, pending KF ms: %d", s_uCurrentKFValue, s_uPendingKFValue);
}

void _adaptive_video_check_apply_capture_video_bitrate(const char* szReason)
{
   int iVideoProfile = g_pCurrentModel->video_params.user_selected_video_link_profile;
   if ( s_uLastVideoProfileRequestedByController != 0xFF )
      iVideoProfile = (int)s_uLastVideoProfileRequestedByController;

   // Update capture video bitrate, qpdelta
   u32 uBitrateBPS = g_pCurrentModel->video_link_profiles[iVideoProfile].bitrate_fixed_bps;

   if ( s_bAdaptiveIsInUplinkLostState )
   {
      uBitrateBPS = (uBitrateBPS * 66) /100;
      if ( uBitrateBPS < DEFAULT_LOWEST_ALLOWED_ADAPTIVE_VIDEO_BITRATE )
         uBitrateBPS = DEFAULT_LOWEST_ALLOWED_ADAPTIVE_VIDEO_BITRATE;
   }
   if ( 0 != s_uAdaptiveVideoTemporaryBitrate )
      uBitrateBPS = s_uAdaptiveVideoTemporaryBitrate;

   log_line("[AdaptiveVideo] Check apply video bitrate (reason: %s): temp bitrate: %u, degraded state? %s, bitrate to set: %u",
      ((szReason!=NULL)?szReason:"N/A"), s_uAdaptiveVideoTemporaryBitrate, s_bAdaptiveIsInUplinkLostState?"yes":"no", uBitrateBPS);

   if ( g_pCurrentModel->hasCamera() )
   if ( g_pCurrentModel->isActiveCameraCSICompatible() || g_pCurrentModel->isActiveCameraVeye() )
      video_source_csi_send_control_message(RASPIVID_COMMAND_ID_VIDEO_BITRATE, uBitrateBPS/100000, 0);
      
   if ( g_pCurrentModel->hasCamera() )
   if ( g_pCurrentModel->isActiveCameraOpenIPC() )
   {
      if ( (uBitrateBPS != hardware_camera_maj_get_current_bitrate()) &&
           (hardware_camera_maj_get_current_qpdelta() != g_pCurrentModel->video_link_profiles[iVideoProfile].iIPQuantizationDelta) )
         hardware_camera_maj_set_bitrate_and_qpdelta( uBitrateBPS, g_pCurrentModel->video_link_profiles[iVideoProfile].iIPQuantizationDelta);
      else if ( uBitrateBPS != hardware_camera_maj_get_current_bitrate() )
         hardware_camera_maj_set_bitrate(uBitrateBPS); 
      else if ( hardware_camera_maj_get_current_qpdelta() != g_pCurrentModel->video_link_profiles[iVideoProfile].iIPQuantizationDelta )
         hardware_camera_maj_set_qpdelta(g_pCurrentModel->video_link_profiles[iVideoProfile].iIPQuantizationDelta);
   }
}

void adaptive_video_set_kf_for_current_video_profile(u16 uKeyframe)
{
   s_uPendingKFValue = uKeyframe;
}

void adaptive_video_set_last_kf_requested_by_controller(u16 uKeyframe)
{
   log_line("[AdaptiveVideo] Set last requested kf by controller: %d ms", uKeyframe);
   s_uPendingKFValue = uKeyframe;
}

void adaptive_video_set_last_profile_requested_by_controller(int iVideoProfile)
{
   if ( s_uLastVideoProfileRequestedByController == iVideoProfile )
   {
      log_line("[AdaptiveVideo] Set new video profile requested by controller: %s, same as current one. Do nothing.", str_get_video_profile_name(iVideoProfile));
      return;
   }

   log_line("[AdaptiveVideo] Set new video profile requested by controller: (%d) %s, current video profile: %d",
       iVideoProfile, str_get_video_profile_name(iVideoProfile), s_uLastVideoProfileRequestedByController);
   log_line("[AdaptiveVideo] Current video profile settings: EC: %d/%d, %d (%d) max bytes in video data, bitrate: %u bps",
      g_pCurrentModel->video_link_profiles[iVideoProfile].iBlockPackets,
      g_pCurrentModel->video_link_profiles[iVideoProfile].iBlockECs,
      g_pCurrentModel->video_link_profiles[iVideoProfile].video_data_length,
      (NULL != g_pVideoTxBuffers)?g_pVideoTxBuffers->getCurrentUsableRawVideoDataSize():0,
      g_pCurrentModel->video_link_profiles[iVideoProfile].bitrate_fixed_bps);

   s_uLastVideoProfileRequestedByController = iVideoProfile;
   if ( NULL != g_pVideoTxBuffers )
   {
      g_pVideoTxBuffers->updateVideoHeader(g_pCurrentModel);
      // To fix when adaptive kf is working 100%
      //s_uPendingKFValue = g_pCurrentModel->getInitialKeyframeIntervalMs(iVideoProfile);
      //log_line("[AdaptiveVideo] Set new KF ms value requested by controller: %d (current KF ms: %d)", s_uPendingKFValue, s_uCurrentKFValue);
   }

   _adaptive_video_check_apply_capture_video_bitrate("set profile from controller");

   // Update adaptive video data rate for tx radio:
   if ( s_uLastVideoProfileRequestedByController == g_pCurrentModel->video_params.user_selected_video_link_profile )
   {
      packet_utils_set_adaptive_video_datarate(0);
      s_iPendingAdaptiveRadioDataRate = 0;
      s_uTimeSetPendingAdaptiveRadioDataRate = 0;
   }
   else
   {
      int nRateTxVideo = DEFAULT_RADIO_DATARATE_VIDEO;
      if ( s_uLastVideoProfileRequestedByController == VIDEO_PROFILE_MQ )
         nRateTxVideo = utils_get_video_profile_mq_radio_datarate(g_pCurrentModel);

      if ( s_uLastVideoProfileRequestedByController == VIDEO_PROFILE_LQ )
         nRateTxVideo = utils_get_video_profile_lq_radio_datarate(g_pCurrentModel);

      // If datarate is increasing, set it right away
      if ( (0 != packet_utils_get_last_set_adaptive_video_datarate()) &&
           (getRealDataRateFromRadioDataRate(nRateTxVideo, 0) >= getRealDataRateFromRadioDataRate(packet_utils_get_last_set_adaptive_video_datarate(), 0)) )
      {
         packet_utils_set_adaptive_video_datarate(nRateTxVideo);
         s_iPendingAdaptiveRadioDataRate = 0;
         s_uTimeSetPendingAdaptiveRadioDataRate = 0;
      }
      // If datarate is decreasing, set it after a short period
      else
      {
         s_iPendingAdaptiveRadioDataRate = nRateTxVideo;
         s_uTimeSetPendingAdaptiveRadioDataRate = g_TimeNow;
      }
   } 

   s_uTimeLastVideoProfileRequestedByController = g_TimeNow;

   log_line("[AdaptiveVideo] Did set new video profile requested by controller: %s", str_get_video_profile_name(iVideoProfile));
}

int adaptive_video_get_current_active_video_profile()
{
   if ( 0xFF != s_uLastVideoProfileRequestedByController )
      return  s_uLastVideoProfileRequestedByController;
   return g_pCurrentModel->video_params.user_selected_video_link_profile;
}

u16 adaptive_video_get_current_kf()
{
   return s_uCurrentKFValue;
}

void _adaptive_video_send_kf_to_capture_program(u16 uNewKeyframeMs)
{
   if ( NULL == g_pCurrentModel )
      return;
   // Send the actual keyframe change to video source/capture

   int iVideoProfile = g_pCurrentModel->video_params.user_selected_video_link_profile;
   if ( 0xFF != s_uLastVideoProfileRequestedByController )
      iVideoProfile = s_uLastVideoProfileRequestedByController;
   
   int iCurrentFPS = g_pCurrentModel->video_link_profiles[iVideoProfile].fps;

   int iKeyFrameCountValue = (iCurrentFPS * (int)uNewKeyframeMs) / 1000; 

   if ( g_pCurrentModel->isActiveCameraCSICompatible() || g_pCurrentModel->isActiveCameraVeye() )
      video_source_csi_send_control_message(RASPIVID_COMMAND_ID_KEYFRAME, (u16)iKeyFrameCountValue, 0);

   if ( g_pCurrentModel->isActiveCameraOpenIPC() )
   {
      float fGOP = 1.0;
      fGOP = ((float)uNewKeyframeMs)/1000.0;
      hardware_camera_maj_set_keyframe(fGOP);                
   }
}

void adaptive_video_set_temporary_bitrate(u32 uBitrateBPS)
{
   if ( ! g_pCurrentModel->hasCamera() )
      return;

   log_line("[AdaptiveVideo] Set temporary video bitrate to %u bps (current temp videobitrate is: %u bps)", uBitrateBPS, s_uAdaptiveVideoTemporaryBitrate);
   s_uAdaptiveVideoTemporaryBitrate = uBitrateBPS;

   _adaptive_video_check_apply_capture_video_bitrate("set temp bitrate");
}

bool adaptive_video_is_in_degraded_state()
{
   return s_bAdaptiveIsInUplinkLostState;
}

u32  adaptive_video_get_degraded_ec_packets_count()
{
   int iVideoProfile = g_pCurrentModel->video_params.user_selected_video_link_profile;
   if ( s_uLastVideoProfileRequestedByController != 0xFF )
      iVideoProfile = (int)s_uLastVideoProfileRequestedByController;
   u32 uECPackets = g_pCurrentModel->video_link_profiles[iVideoProfile].iBlockECs;
   u32 uMidEC = (u32) rintf((float)g_pCurrentModel->video_link_profiles[iVideoProfile].iBlockPackets/2.0);
   if ( uECPackets < uMidEC )
   {
      uECPackets = uMidEC;
      if ( uECPackets == 0 )
         uECPackets = 1;
   }
   return uECPackets;
}


void adaptive_video_on_uplink_lost()
{
   if ( s_bAdaptiveIsInUplinkLostState )
      return;

   log_line("[AdaptiveVideo] on uplink lost.");
   s_bAdaptiveIsInUplinkLostState = true;
   _adaptive_video_check_apply_capture_video_bitrate("on uplink lost");
}

void adaptive_video_on_uplink_recovered()
{
   if ( ! s_bAdaptiveIsInUplinkLostState )
      return;
   log_line("[AdaptiveVideo] on uplink recovered.");
   s_bAdaptiveIsInUplinkLostState = false;
   _adaptive_video_check_apply_capture_video_bitrate("on uplink recovered");
}

void adaptive_video_on_user_video_bitrate_changed(u32 uOldBitrate, u32 uNewBitrate)
{
   if ( adaptive_video_get_current_active_video_profile() != g_pCurrentModel->video_params.user_selected_video_link_profile )
      return;
   if ( uOldBitrate == uNewBitrate )
      return;

   _adaptive_video_check_apply_capture_video_bitrate("user video profile bitrate changed");
}

void adaptive_video_on_capture_restarted()
{
}

void adaptive_video_on_new_camera_read(bool bIsEndOfFrame)
{
   if ( 0 != s_uPendingKFValue )
   {
      if ( s_uPendingKFValue == s_uCurrentKFValue )
         s_uPendingKFValue = 0;
   }
   if ( s_uPendingKFValue != 0 )
   if ( s_uPendingKFValue != s_uCurrentKFValue )
   if ( NULL != g_pVideoTxBuffers )
   if ( bIsEndOfFrame )
   {
      _adaptive_video_send_kf_to_capture_program(s_uPendingKFValue);
      log_line("[AdaptiveVideo] Changed KF ms value from %d to %d", s_uCurrentKFValue, s_uPendingKFValue);
      s_uCurrentKFValue = s_uPendingKFValue;
      s_uPendingKFValue = 0;
      g_pVideoTxBuffers->updateCurrentKFValue();
   }
}

void adaptive_video_periodic_loop()
{
   if ( g_TimeNow < s_uTimeLastTimeAdaptivePeriodicLoop + 10 )
      return;
   if ( negociate_radio_link_is_in_progress() || test_link_is_in_progress() )
      return;
   
   s_uTimeLastTimeAdaptivePeriodicLoop = g_TimeNow;
   
   if ( 0 != s_uAdaptiveTimeToSetInitialBitrate )
   if ( g_TimeNow >= s_uAdaptiveTimeToSetInitialBitrate )
   {
      s_uAdaptiveTimeToSetInitialBitrate = 0;
      _adaptive_video_check_apply_capture_video_bitrate("initial set");
   }

   u32 uDeltaTime = DEFAULT_LOWER_VIDEO_RADIO_DATARATE_AFTER_MS;
   #if defined HW_PLATFORM_RASPBERRY
   uDeltaTime *= 2;
   #endif

   if ( (0 != s_iPendingAdaptiveRadioDataRate) && (0 != s_uTimeSetPendingAdaptiveRadioDataRate) )
   if ( g_TimeNow >= s_uTimeSetPendingAdaptiveRadioDataRate + uDeltaTime )
   {
      packet_utils_set_adaptive_video_datarate(s_iPendingAdaptiveRadioDataRate);
      s_iPendingAdaptiveRadioDataRate = 0;
      s_uTimeSetPendingAdaptiveRadioDataRate = 0;
   }
}