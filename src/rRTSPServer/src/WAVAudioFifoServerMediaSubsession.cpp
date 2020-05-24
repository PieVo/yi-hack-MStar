/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "liveMedia"
// Copyright (c) 1996-2020 Live Networks, Inc.  All rights reserved.
// A 'ServerMediaSubsession' object that creates new, unicast, "RTPSink"s
// on demand, from an WAV fifo file.
// Implementation

#include "WAVAudioFifoServerMediaSubsession.hh"
#include "WAVAudioFifoSource.hh"
#include "uLawAudioFilter.hh"
#include "SimpleRTPSink.hh"
#include "YiNoiseReduction.hh"

WAVAudioFifoServerMediaSubsession* WAVAudioFifoServerMediaSubsession
::createNew(UsageEnvironment& env, StreamReplicator* replicator, Boolean reuseFirstSource, Boolean convertToULaw) {
  return new WAVAudioFifoServerMediaSubsession(env, replicator, reuseFirstSource, convertToULaw);
}

WAVAudioFifoServerMediaSubsession
::WAVAudioFifoServerMediaSubsession(UsageEnvironment& env, StreamReplicator* replicator, Boolean reuseFirstSource, Boolean convertToULaw)
  : OnDemandServerMediaSubsession(env, reuseFirstSource),
    fReplicator(replicator),
    fConvertToULaw(convertToULaw) {
}

WAVAudioFifoServerMediaSubsession
::~WAVAudioFifoServerMediaSubsession() {
}

void WAVAudioFifoServerMediaSubsession
::seekStreamSource(FramedSource* inputSource, double& seekNPT, double streamDuration, u_int64_t& numBytes) {
  WAVAudioFifoSource* wavSource;
  if (fBitsPerSample > 8) {
    // "inputSource" is a filter; its input source is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)(((FramedFilter*)inputSource)->inputSource());
  } else {
    // "inputSource" is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)inputSource;
  }

  unsigned seekSampleNumber = (unsigned)(seekNPT*fSamplingFrequency);
  unsigned seekByteNumber = seekSampleNumber*((fNumChannels*fBitsPerSample)/8);

  wavSource->seekToPCMByte(seekByteNumber);

  setStreamSourceDuration(inputSource, streamDuration, numBytes);
}

void WAVAudioFifoServerMediaSubsession
::setStreamSourceDuration(FramedSource* inputSource, double streamDuration, u_int64_t& numBytes) {
  WAVAudioFifoSource* wavSource;
  if (fBitsPerSample > 8) {
    // "inputSource" is a filter; its input source is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)(((FramedFilter*)inputSource)->inputSource());
  } else {
    // "inputSource" is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)inputSource;
  }

  unsigned numDurationSamples = (unsigned)(streamDuration*fSamplingFrequency);
  unsigned numDurationBytes = numDurationSamples*((fNumChannels*fBitsPerSample)/8);
  numBytes = (u_int64_t)numDurationBytes;

  wavSource->limitNumBytesToStream(numDurationBytes);
}

void WAVAudioFifoServerMediaSubsession
::setStreamSourceScale(FramedSource* inputSource, float scale) {
  int iScale = (int)scale;
  WAVAudioFifoSource* wavSource;
  if (fBitsPerSample > 8) {
    // "inputSource" is a filter; its input source is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)(((FramedFilter*)inputSource)->inputSource());
  } else {
    // "inputSource" is the original WAV file source:
    wavSource = (WAVAudioFifoSource*)inputSource;
  }

  wavSource->setScaleFactor(iScale);
}

FramedSource* WAVAudioFifoServerMediaSubsession
::createNewStreamSource(unsigned /*clientSessionId*/, unsigned& estBitrate) {
  FramedSource* resultSource = NULL;
  WAVAudioFifoSource* originalSource = NULL;

  // The amount of filters depends on wheter noise reduction is enabled
  FramedFilter* filterSource1 = (FramedFilter*)fReplicator->inputSource();
  // Source of endian-swap or uLaw filter
  FramedFilter* filterSource2 = (FramedFilter*)filterSource1->inputSource();
  // Source of noise reduction filter
  FramedFilter* filterSource3 = (FramedFilter*)filterSource2->inputSource();

  if (((WAVAudioFifoSource*)(filterSource2))->bitsPerSample() != 0) {
      originalSource = (WAVAudioFifoSource*)(filterSource2);
  } else {
      originalSource = (WAVAudioFifoSource*)(filterSource3);
  }
  
  printf("fReplicator->inputSource() = %p\n", originalSource);
  resultSource = fReplicator->createStreamReplica();
  if (resultSource == NULL) {
    printf("Failed to create stream replica\n");
    Medium::close(resultSource);
    return NULL;
  }
  else {
    fAudioFormat = originalSource->getAudioFormat();
    fBitsPerSample = originalSource->bitsPerSample();
    fSamplingFrequency = originalSource->samplingFrequency();
    fConvertToULaw = True;
    fNumChannels = originalSource->numChannels();
    unsigned bitsPerSecond = fSamplingFrequency*fBitsPerSample*fNumChannels;
    printf("Original source FMT: %d bps: %d freq: %d\n", fAudioFormat, fBitsPerSample, fSamplingFrequency);
    fFileDuration = ~0;//(float)((8.0*originalSource->numPCMBytes())/(fSamplingFrequency*fNumChannels*fBitsPerSample));

    estBitrate = (bitsPerSecond+500)/1000; // kbps

    if (fConvertToULaw)
        estBitrate /= 2;

    return resultSource;
  }
}

RTPSink* WAVAudioFifoServerMediaSubsession
::createNewRTPSink(Groupsock* rtpGroupsock,
		   unsigned char rtpPayloadTypeIfDynamic,
		   FramedSource* /*inputSource*/) {
  do {
    char const* mimeType;
    unsigned char payloadFormatCode = rtpPayloadTypeIfDynamic; // by default, unless a static RTP payload type can be used
    if (fAudioFormat == WA_PCM) {
      if (fBitsPerSample == 16) {
	if (fConvertToULaw) {
	  mimeType = "PCMU";
	  if (fSamplingFrequency == 8000 && fNumChannels == 1) {
	    payloadFormatCode = 0; // a static RTP payload type
	  }
	} else {
	  mimeType = "L16";
	  if (fSamplingFrequency == 44100 && fNumChannels == 2) {
	    payloadFormatCode = 10; // a static RTP payload type
	  } else if (fSamplingFrequency == 44100 && fNumChannels == 1) {
	    payloadFormatCode = 11; // a static RTP payload type
	  }
	}
      } else if (fBitsPerSample == 20) {
	mimeType = "L20";
      } else if (fBitsPerSample == 24) {
	mimeType = "L24";
      } else { // fBitsPerSample == 8 (we assume that fBitsPerSample == 4 is only for WA_IMA_ADPCM)
	mimeType = "L8";
      }
    } else if (fAudioFormat == WA_PCMU) {
      mimeType = "PCMU";
      if (fSamplingFrequency == 8000 && fNumChannels == 1) {
	payloadFormatCode = 0; // a static RTP payload type
      }
    } else if (fAudioFormat == WA_PCMA) {
      mimeType = "PCMA";
      if (fSamplingFrequency == 8000 && fNumChannels == 1) {
	payloadFormatCode = 8; // a static RTP payload type
      }
    } else if (fAudioFormat == WA_IMA_ADPCM) {
      mimeType = "DVI4";
      // Use a static payload type, if one is defined:
      if (fNumChannels == 1) {
	if (fSamplingFrequency == 8000) {
	  payloadFormatCode = 5; // a static RTP payload type
	} else if (fSamplingFrequency == 16000) {
	  payloadFormatCode = 6; // a static RTP payload type
	} else if (fSamplingFrequency == 11025) {
	  payloadFormatCode = 16; // a static RTP payload type
	} else if (fSamplingFrequency == 22050) {
	  payloadFormatCode = 17; // a static RTP payload type
	}
      }
    } else { //unknown format
      break;
    }
    printf("Create SimpleRTPSink: %s, freq: %d, channels %d\n", mimeType, fSamplingFrequency, fNumChannels);
    return SimpleRTPSink::createNew(envir(), rtpGroupsock,
				    payloadFormatCode, fSamplingFrequency,
				    "audio", mimeType, fNumChannels);
  } while (0);

  // An error occurred:
  return NULL;
}

void WAVAudioFifoServerMediaSubsession::testScaleFactor(float& scale) {
  if (fFileDuration <= 0.0) {
    // The file is non-seekable, so is probably a live input source.
    // We don't support scale factors other than 1
    scale = 1;
  } else {
    // We support any integral scale, other than 0
    int iScale = scale < 0.0 ? (int)(scale - 0.5) : (int)(scale + 0.5); // round
    if (iScale == 0) iScale = 1;
    scale = (float)iScale;
  }
}

float WAVAudioFifoServerMediaSubsession::duration() const {
  return fFileDuration;
}
