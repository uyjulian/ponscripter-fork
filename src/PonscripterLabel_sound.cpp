/* -*- C++ -*-
 *
 *  PonscripterLabel_sound.cpp - Methods for playing sound
 *
 *  Copyright (c) 2001-2008 Ogapee (original ONScripter, of which this
 *  is a fork).
 *
 *  ogapee@aqua.dti2.ne.jp
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307 USA
 */

#include "PonscripterLabel.h"
#include "PonscripterUserEvents.h"
#ifdef LINUX
#include <signal.h>
#endif

#ifdef USE_AVIFILE
#include "AVIWrapper.h"
#endif

struct WAVE_HEADER {
    char chunk_riff[4];
    char riff_length[4];
    char fmt_id[8];
    char fmt_size[4];
    char data_fmt[2];
    char channels[2];
    char frequency[4];
    char byte_size[4];
    char sample_byte_size[2];
    char sample_bit_size[2];

    char chunk_id[4];
    char data_length[4];
} header;

typedef struct
{
  SMPEG_Frame *frame;
  int dirty;
  SDL_mutex *lock;
} update_context;

extern bool ext_music_play_once_flag;

extern "C" {
    extern void mp3callback(void* userdata, Uint8 * stream, int len);

    extern void oggcallback(void* userdata, Uint8 * stream, int len);

#ifdef MACOSX
    extern Uint32 SDLCALL midiSDLCallback(Uint32 interval, void* param);

#endif
}
extern void midiCallback(int sig);
extern void musicCallback(int sig);

#ifdef MACOSX
extern SDL_TimerID timer_midi_id;
#endif

#define TMP_MIDI_FILE "tmp.mid"
#define TMP_MUSIC_FILE "tmp.mus"

#define SWAP_SHORT_BYTES(sptr){          \
            Uint8 *bptr = (Uint8 *)sptr; \
            Uint8 tmpb = *bptr;          \
            *bptr = *(bptr+1);           \
            *(bptr+1) = tmpb;            \
        }

extern long decodeOggVorbis(PonscripterLabel::MusicStruct *music_struct, Uint8 *buf_dst, long len, bool do_rate_conversion)
{
    int  current_section;
    long total_len = 0;

    OVInfo *ovi = music_struct->ovi;
    char* buf = (char*) buf_dst;
    if (do_rate_conversion && ovi->cvt.needed) {
        len = len * ovi->mult1 / ovi->mult2;
        if (ovi->cvt_len < len * ovi->cvt.len_mult) {
            if (ovi->cvt.buf) delete[] ovi->cvt.buf;

            ovi->cvt.buf = new unsigned char[len * ovi->cvt.len_mult];
            ovi->cvt_len = len * ovi->cvt.len_mult;
        }

        buf = (char*) ovi->cvt.buf;
    }

#ifdef USE_OGG_VORBIS
    while (1) {
#ifdef INTEGER_OGG_VORBIS
        long src_len = ov_read(&ovi->ovf, buf, len, &current_section);
#else
        long src_len = ov_read(&ovi->ovf, buf, len, 0, 2, 1, &current_section);
#endif

        if (ovi->loop == 1) {
            ogg_int64_t pcmPos = ov_pcm_tell(&ovi->ovf);
            if (pcmPos >= ovi->loop_end) {
                len -= ((pcmPos - ovi->loop_end) * ovi->channels) * (long)sizeof(Uint16);
                ov_pcm_seek(&ovi->ovf, ovi->loop_start);
            }
        }
        if (src_len <= 0) break;

        int vol = music_struct->is_mute ? 0 : music_struct->volume;
        long dst_len = src_len;
        if (do_rate_conversion && ovi->cvt.needed){
            ovi->cvt.len = src_len;
            SDL_ConvertAudio(&ovi->cvt);
            memcpy(buf_dst, ovi->cvt.buf, ovi->cvt.len_cvt);
            dst_len = ovi->cvt.len_cvt;

            if (vol != DEFAULT_VOLUME){
                // volume change under SOUND_OGG_STREAMING
                for (int i=0 ; i<dst_len ; i+=2){
                    short a = *(short*)(buf_dst+i);
                    a = a*vol/100;
                    *(short*)(buf_dst+i) = a;
                }
            }
            buf_dst += ovi->cvt.len_cvt;
        }
        else{
            if (do_rate_conversion && vol != DEFAULT_VOLUME){
                // volume change under SOUND_OGG_STREAMING
                for (int i=0 ; i<dst_len ; i+=2){
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    SWAP_SHORT_BYTES( ((short*)(buf_dst+i)) )
#endif
                    short a = *(short*)(buf_dst+i);
                    a = a*vol/100;
                    *(short*)(buf_dst+i) = a;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
                    SWAP_SHORT_BYTES( ((short*)(buf_dst+i)) )
#endif
                }
            }
            buf += dst_len;
            buf_dst += dst_len;
        }

        total_len += dst_len;
        if (src_len == len) break;
        len -= src_len;
    }
#endif

    return total_len;
}


int PonscripterLabel::playSound(const pstring& filename, int format,
                                bool loop_flag, int channel)
{
    if ( !audio_open_flag ) return SOUND_NONE;
    if (filename.length() == 0) return SOUND_NONE;

    long length = script_h.cBR->getFileLength( filename );
    if (length == 0) {
        errorAndCont(filename + " not found");
        return SOUND_NONE;
    }

    //Mion: account for mode_wave_demo setting
    //(i.e. if not set, then don't play non-bgm wave/ogg during skip mode)
    if (!mode_wave_demo_flag &&
        ( skip_flag || ctrl_pressed_status )) {
        if ((format & (SOUND_OGG | SOUND_WAVE)) &&
            ((channel < ONS_MIX_CHANNELS) || (channel == MIX_WAVE_CHANNEL) ||
             (channel == MIX_CLICKVOICE_CHANNEL)))
            return SOUND_NONE;
    }

    unsigned char* buffer;

    if ((format & (SOUND_MP3 | SOUND_OGG_STREAMING)) &&
        (length == music_buffer_length) &&
        music_buffer ){
        buffer = music_buffer;
    }
    else{
        buffer = new unsigned char[length];
        script_h.cBR->getFile( filename, buffer );
    }

    if (format & (SOUND_OGG | SOUND_OGG_STREAMING)) {
        int ret = playOGG(format, buffer, length, loop_flag, channel);
        if (ret & (SOUND_OGG | SOUND_OGG_STREAMING)) return ret;
    }

    if (format & SOUND_WAVE) {
        Mix_Chunk* chunk = Mix_LoadWAV_RW(SDL_RWFromMem(buffer, length), 1);
        if (playWave(chunk, format, loop_flag, channel) == 0) {
            delete[] buffer;
            return SOUND_WAVE;
        }
    }

    if (format & SOUND_MP3) {
        if (music_cmd) {
            FILE* fp = fopen(script_h.save_path + TMP_MUSIC_FILE, "wb");
            if (fp == NULL) {
                fprintf(stderr, "can't open temporary music file %s\n",
                        TMP_MUSIC_FILE);
            }
            else {
                fwrite(buffer, 1, length, fp);
                fclose(fp);
                ext_music_play_once_flag = !loop_flag;
                if (playExternalMusic(loop_flag) == 0) {
                    music_buffer = buffer;
                    music_buffer_length = length;
                    return SOUND_MP3;
                }
            }
        }

        mp3_sample = SMPEG_new_rwops(SDL_RWFromMem(buffer, length), NULL, 0, 0);
        if (playMP3() == 0) {
            music_buffer = buffer;
            music_buffer_length = length;
            return SOUND_MP3;
        }
    }

    /* check WMA */
    if (buffer[0] == 0x30 && buffer[1] == 0x26
        && buffer[2] == 0xb2 && buffer[3] == 0x75) {
        delete[] buffer;
        return SOUND_OTHER;
    }

    if (format & SOUND_MIDI) {
        FILE* fp = fopen(script_h.save_path + TMP_MIDI_FILE, "wb");
        if (fp == NULL) {
            fprintf(stderr, "can't open temporary MIDI file %s\n",
                    TMP_MIDI_FILE);
        }
        else {
            fwrite(buffer, 1, length, fp);
            fclose(fp);
            ext_music_play_once_flag = !loop_flag;
            if (playMIDI(loop_flag) == 0) {
                delete[] buffer;
                return SOUND_MIDI;
            }
        }
    }

    delete[] buffer;

    return SOUND_OTHER;
}


int PonscripterLabel::playWave(Mix_Chunk* chunk, int format, bool loop_flag,
			       int channel)
{
    if (!chunk) return -1;

    Mix_Pause(channel);
    if (wave_sample[channel]) Mix_FreeChunk(wave_sample[channel]);

    wave_sample[channel] = chunk;

    if (channel == 0)
        Mix_Volume(channel, !volume_on_flag? 0 : voice_volume * 128 / 100);
    else if (channel == MIX_BGM_CHANNEL)
        Mix_Volume(channel, !volume_on_flag? 0 : music_volume * 128 / 100);
    else
        Mix_Volume(channel, !volume_on_flag? 0 : se_volume * 128 / 100);

    if (!(format & SOUND_PRELOAD))
        Mix_PlayChannel(channel, wave_sample[channel], loop_flag ? -1 : 0);

    return 0;
}


int PonscripterLabel::playMP3()
{
    if (SMPEG_error(mp3_sample)) {
        //printf(" failed. [%s]\n",SMPEG_error( mp3_sample ));
        // The line below fails. ?????
        //SMPEG_delete( mp3_sample );
        mp3_sample = NULL;
        return -1;
    }

#ifndef MP3_MAD
    //Mion - SMPEG doesn't handle different audio spec well, so we might
    // reset the SDL mixer
    SDL_AudioSpec wanted;
    SMPEG_wantedSpec( mp3_sample, &wanted );
    if ((wanted.format != audio_format.format) ||
        (wanted.freq != audio_format.freq)) {
        Mix_CloseAudio();
        openAudio(wanted.freq, wanted.format, wanted.channels);
        if (!audio_open_flag) {
            // didn't work, use the old settings
            openAudio();
       }
    }
    SMPEG_enableaudio( mp3_sample, 0 );
    SMPEG_actualSpec( mp3_sample, &audio_format );
    SMPEG_enableaudio( mp3_sample, 1 );
#endif
    SMPEG_setvolume( mp3_sample, !volume_on_flag? 0 : music_volume );
    Mix_HookMusic( mp3callback, mp3_sample );
    SMPEG_play( mp3_sample );

    return 0;
}


int PonscripterLabel::playOGG(int format, unsigned char* buffer, long length, bool loop_flag, int channel)
{
    int channels, rate;
    OVInfo* ovi = openOggVorbis(buffer, length, channels, rate);
    if (ovi == NULL) return SOUND_OTHER;

    if (format & SOUND_OGG) {
        unsigned char* buffer2 = new unsigned char[sizeof(WAVE_HEADER) + ovi->decoded_length];

        MusicStruct ms;
        ms.ovi = ovi;
        ms.voice_sample = NULL;
        ms.volume = channelvolumes[channel];
        decodeOggVorbis(&ms, buffer2 + sizeof(WAVE_HEADER), ovi->decoded_length, false);
        setupWaveHeader(buffer2, channels, rate, 16, ovi->decoded_length);
        Mix_Chunk* chunk = Mix_LoadWAV_RW(SDL_RWFromMem(buffer2, sizeof(WAVE_HEADER) + ovi->decoded_length), 1);
        delete[] buffer2;
        closeOggVorbis(ovi);
        delete[] buffer;

        playWave(chunk, format, loop_flag, channel);

        return SOUND_OGG;
    }

    if ((audio_format.format != AUDIO_S16) ||
        (audio_format.freq != rate)) {
        Mix_CloseAudio();
        openAudio(rate, AUDIO_S16, channels);
        ovi->cvt.needed = 0;
        if (!audio_open_flag) {
            // didn't work, use the old settings
            openAudio();
            ovi->cvt_len = 0;
            SDL_BuildAudioCVT(&ovi->cvt,
                      AUDIO_S16, channels, rate,
                      audio_format.format, audio_format.channels, audio_format.freq);
            ovi->mult1 = 10;
            ovi->mult2 = (int)(ovi->cvt.len_ratio*10.0);
       }
    }

    music_struct.ovi = ovi;
    music_struct.volume = music_volume;
    music_struct.is_mute = !volume_on_flag;
    Mix_HookMusic(oggcallback, &music_struct);

    music_buffer = buffer;
    music_buffer_length = length;

    return SOUND_OGG_STREAMING;
}


int PonscripterLabel::playExternalMusic(bool loop_flag)
{
    int music_looping = loop_flag ? -1 : 0;
#ifdef LINUX
    signal(SIGCHLD, musicCallback);
    if (music_cmd) music_looping = 0;

#endif

    Mix_SetMusicCMD(music_cmd);

    pstring music_filename = script_h.save_path + TMP_MUSIC_FILE;
    if ((music_info = Mix_LoadMUS(music_filename)) == NULL) {
        fprintf(stderr, "can't load Music file %s\n",
		(const char*) music_filename);
        return -1;
    }

    // Mix_VolumeMusic( music_volume );
    Mix_PlayMusic(music_info, music_looping);

    return 0;
}


int PonscripterLabel::playMIDI(bool loop_flag)
{
    Mix_SetMusicCMD(midi_cmd);

    pstring midi_filename = script_h.save_path + TMP_MIDI_FILE;
    if ((midi_info = Mix_LoadMUS(midi_filename)) == NULL) return -1;

#ifndef MACOSX
    int midi_looping = loop_flag ? -1 : 0;
#endif

#ifdef EXTERNAL_MIDI_PROGRAM
    FILE* com_file;
    if (midi_play_loop_flag) {
        if ((com_file = fopen("play_midi", "wb")) != NULL)
            fclose(com_file);
    }
    else {
        if ((com_file = fopen("playonce_midi", "wb")) != NULL)
            fclose(com_file);
    }

#endif

#ifdef LINUX
    signal(SIGCHLD, midiCallback);
    if (midi_cmd) midi_looping = 0;

#endif

    Mix_VolumeMusic(!volume_on_flag? 0 : music_volume);
#ifdef MACOSX
    // Emulate looping on MacOS ourselves to work around bug in SDL_Mixer
    Mix_PlayMusic(midi_info, false);
    timer_midi_id = SDL_AddTimer(1000, midiSDLCallback, NULL);
#else
    Mix_PlayMusic(midi_info, midi_looping);
#endif

    return 0;
}


int PonscripterLabel::playingMusic()
{
    if ((Mix_GetMusicHookData() != NULL) || (Mix_Playing(MIX_BGM_CHANNEL) == 1)
        || (Mix_PlayingMusic() == 1))
        return 1;
    else
        return 0;
}

int PonscripterLabel::setCurMusicVolume( int volume )
{
    if (Mix_GetMusicHookData() != NULL) { // for streamed MP3 & OGG
        if ( mp3_sample ) SMPEG_setvolume( mp3_sample, !volume_on_flag? 0 : volume ); // mp3
        else music_struct.volume = volume; // ogg
    } else if (Mix_Playing(MIX_BGM_CHANNEL) == 1) { // wave
        Mix_Volume( MIX_BGM_CHANNEL, !volume_on_flag? 0 : volume * 128 / 100 );
    } else if (Mix_PlayingMusic() == 1) { // midi
        Mix_VolumeMusic( !volume_on_flag? 0 : volume * 128 / 100 );
    }

    return 0;
}

int PonscripterLabel::setVolumeMute( bool do_mute )
{
    if (Mix_GetMusicHookData() != NULL) { // for streamed MP3 & OGG
        if ( mp3_sample ) SMPEG_setvolume( mp3_sample, do_mute? 0 : music_volume ); // mp3
        else music_struct.is_mute = do_mute; // ogg
    } else if (Mix_Playing(MIX_BGM_CHANNEL) == 1) { // wave
        Mix_Volume( MIX_BGM_CHANNEL, do_mute? 0 : music_volume * 128 / 100 );
    } else if (Mix_PlayingMusic() == 1) { // midi
        Mix_VolumeMusic( do_mute? 0 : music_volume * 128 / 100 );
    }
    for ( int i=1 ; i<ONS_MIX_CHANNELS ; i++ ) {
        if ( wave_sample[i] )
            Mix_Volume( i, do_mute? 0 : channelvolumes[i] * 128 / 100 );
     }
    if ( wave_sample[MIX_LOOPBGM_CHANNEL0] )
        Mix_Volume( MIX_LOOPBGM_CHANNEL0, do_mute? 0 : se_volume * 128 / 100 );
    if ( wave_sample[MIX_LOOPBGM_CHANNEL1] )
        Mix_Volume( MIX_LOOPBGM_CHANNEL1, do_mute? 0 : se_volume * 128 / 100 );

    return 0;
}

// We assume only one MPEG video will ever play at a time.
// This bit is messy, but it seems we cannot use a method here, so we
// simply must shift all this stuff into plain C variables.
struct SubAndTexture {
  AnimationInfo *ai;
  SDL_Texture *tex;
};
typedef std::vector<SubAndTexture*> olvec;
static olvec overlays;
static SDL_Texture *video_texture = NULL;
/*
   Each of these is only needed if there are subs.
   Otherwise we can just pop it straight on the screen
 */
static SDL_Renderer *video_renderer = NULL;

void UpdateMPEG(void *data, SMPEG_Frame *frame) {
  update_context *c = (update_context *)data;
  c->frame = frame;
  // Let ourselves know we've got a new frame to render
  c->dirty = 1;
}

int PonscripterLabel::playMPEG(const pstring& filename, bool click_flag, bool loop_flag,
                               bool mixsound_flag, bool nosound_flag, SubtitleDefs& subtitles)
{
    int ret = 0;
#ifndef MP3_MAD
    bool different_spec = false;
    pstring mpeg_dat = ScriptHandler::cBR->getFile(filename);
    SMPEG* mpeg_sample = SMPEG_new_rwops(rwops(mpeg_dat), 0, 0, 0);
    if (!SMPEG_error(mpeg_sample)) {
        SMPEG_enableaudio(mpeg_sample, 0);

        if (audio_open_flag && !nosound_flag) {
            //Mion - SMPEG doesn't handle different audio spec well, so
            // let's redo the SDL mixer just for this video playback
            SDL_AudioSpec wanted;
            SMPEG_wantedSpec(mpeg_sample, &wanted);
            if (!mixsound_flag && (wanted.format != audio_format.format ||
                wanted.freq != audio_format.freq))
            {
                different_spec = true;
                Mix_CloseAudio();
                openAudio(wanted.freq, wanted.format, wanted.channels);
                if (!audio_open_flag) {
                  fprintf(stderr, "New format error, using old\n");
                    openAudio();
                    different_spec = false;
                }
            }
            SMPEG_actualSpec(mpeg_sample, &audio_format);
            SMPEG_enableaudio(mpeg_sample, 1);
        }

        SMPEG_enablevideo(mpeg_sample, 1);

        update_context c;
        c.dirty = 0;
        c.lock = SDL_CreateMutex();

        /* SMPEG wants the width to be a multiple of 16 */
        int texture_width = (screen_width + 15) & ~15;
        int texture_height = (screen_height + 15) & ~15;


        /* Video texture contains each video frame which is then rendered */
        video_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STREAMING, texture_width, texture_height);
        SDL_SetTextureAlphaMod(video_texture, SDL_ALPHA_OPAQUE);
        SDL_SetTextureBlendMode(video_texture, SDL_BLENDMODE_BLEND);

        if(subtitles) {
          /* Pairs of sub-AnimationInfos and sub-textures. The texture is rendered on top of the video_texture */
          overlays.assign(size_t(subtitles.numdefs()), NULL);
        }

        SMPEG_setdisplay(mpeg_sample, UpdateMPEG, &c, c.lock);


        if (!nosound_flag) {
            SMPEG_setvolume(mpeg_sample, !volume_on_flag? 0 : music_volume);
            Mix_HookMusic(mp3callback, mpeg_sample);
        }

        if (loop_flag) {
            SMPEG_loop(mpeg_sample, -1);
        }
        SMPEG_play(mpeg_sample);

        bool done_flag = false;
        bool interrupted_redraw = false;
        bool done_click_down = false;

        while (!done_flag)
        {
            if (SMPEG_status(mpeg_sample) != SMPEG_PLAYING) {
                if (loop_flag) {
                    SMPEG_play( mpeg_sample );
                } else {
                    break;
                }
            }

            SDL_Event event, tmp_event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_KEYUP: {
                    int s = ((SDL_KeyboardEvent*) &event)->keysym.sym;
                    if (s == SDLK_RETURN || s == SDLK_SPACE || s == SDLK_ESCAPE)
                        done_flag = click_flag;
                    if (s == SDLK_m) {
                        volume_on_flag = !volume_on_flag;
                        SMPEG_setvolume(mpeg_sample, !volume_on_flag? 0 : music_volume);
                        printf("turned %s volume mute\n", !volume_on_flag?"on":"off");
                    }
                    if (s == SDLK_f) {
                        if (fullscreen_mode) menu_windowCommand("menu_window");
                        else menu_fullCommand("menu_full");
                    }
                    break;
                }
                case SDL_QUIT:
                    ret = 1;
                    done_flag = true;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    done_click_down = true;
                    break;
                case SDL_MOUSEMOTION:
                    done_click_down = false;
                    break;
                case SDL_MOUSEBUTTONUP:
                    if(done_click_down)
                        done_flag = click_flag;
                    break;
                case INTERNAL_REDRAW_EVENT:
                    interrupted_redraw = true;
                    break;
                case SDL_WINDOWEVENT:
                    switch(event.window.event) {
                        case SDL_WINDOWEVENT_MAXIMIZED:
                        case SDL_WINDOWEVENT_RESIZED:
                            SDL_PumpEvents();
                            SDL_PeepEvents(&tmp_event, 1, SDL_GETEVENT, SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONDOWN);
                            done_click_down = false;
                            break;
                    }
                default:
                    break;
                }
            }

            if (subtitles) {
                SMPEG_Info info;
                SMPEG_getinfo(mpeg_sample, &info);
                if (info.current_time >= subtitles.next()) {
                    Subtitle s = subtitles.pop();

                    if (overlays[s.number]) {
                      if(overlays[s.number]->tex) {
                        SDL_DestroyTexture(overlays[s.number]->tex);
                        overlays[s.number]->tex = NULL;
                      }
                      if(overlays[s.number]->ai) {
                        delete overlays[s.number]->ai;
                        overlays[s.number]->ai = NULL;
                      }
                      delete overlays[s.number];
                      overlays[s.number] = NULL;
                    }

                    AnimationInfo* overlay = 0;
                    SDL_Texture *overlay_tex = NULL;


                    if (s.text) {
                        overlay = new AnimationInfo();
                        overlay->setImageName(s.text);
                        overlay->pos.x = screen_width / 2;
                        overlay->pos.y = subtitles.pos(s.number);
                        parseTaggedString(overlay);
                        overlay->color_list[0] = subtitles.colour(s.number);
                        setupAnimationInfo(overlay);
                        overlay->trans = subtitles.alpha(s.number);

                        /* Create the texture that we'll use to render this */
                        overlay_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, texture_width, texture_height);
                        SDL_UpdateTexture(overlay_tex, &overlay->pos, overlay->image_surface->pixels, overlay->image_surface->pitch);
                        SDL_SetTextureBlendMode(overlay_tex, SDL_BLENDMODE_BLEND);
                        SDL_SetTextureAlphaMod(overlay_tex, overlay->trans);


                        overlays[s.number] = new SubAndTexture();
                        overlays[s.number]->ai = overlay;
                        overlays[s.number]->tex = overlay_tex;
                    }
                }
            }
            if(c.dirty) {
              SDL_mutexP(c.lock);
              c.dirty = 0; //Flag that we're handling this; if a new frame appears we should deal with it too.

              SDL_RenderClear(renderer); // stops flickering garbage

              SDL_Rect r;
              r.x = 0; r.y = 0; r.w = c.frame->image_width; r.h = c.frame->image_height;
              SDL_UpdateTexture(video_texture, &r, c.frame->image, c.frame->image_width);
#ifdef USE_2X_MODE
              //Mion: so 2X Umineko will play its video correctly
              // (note: when adding proper "movie" cmd support, will probably
              // use some specialized variation of the video_texture
              // and/or the SDL_Rect for RenderCopy to handle pos&size args)
              SDL_Rect r2;
              // chronotrig: Bumping the video 2px down and to the right to 
              // hide unsightly green line, should probably be cleaned up
              // chronotrig again: now this is causing trouble for windows only
              // Removing the old change to see if it fixes anything
              r2.x = -2; r2.y = -2; r2.w = r.w * 2 + 4; r2.h = r.h * 2 + 4;
              SDL_RenderCopy(renderer, video_texture, &r, &r2);
#else
              SDL_RenderCopy(renderer, video_texture, &r, &r);
#endif

              if(subtitles) {
                /* render any subs onto the screen */
                for(olvec::iterator it = overlays.begin(); it != overlays.end(); ++it) {
                  if((*it) && (*it)->ai && (*it)->tex) {
                    SDL_RenderCopy(renderer, (*it)->tex, &(*it)->ai->pos, &(*it)->ai->pos);
                  }
                }
              }

              SDL_mutexV(c.lock);
              SDL_RenderPresent(renderer);
            }
            SDL_Delay(10);
        }

        ctrl_pressed_status = 0;

        SMPEG_stop(mpeg_sample);
        if (!nosound_flag) {
            Mix_HookMusic(NULL, NULL);
        }
        SMPEG_delete(mpeg_sample);
        SDL_DestroyTexture(video_texture);
        video_texture = NULL;

        if (different_spec) {
            //restart mixer with the old audio spec
            Mix_CloseAudio();
            openAudio();
        }

        if (video_renderer) {
            SDL_DestroyRenderer(video_renderer);
            video_renderer = NULL;
        }
        for (olvec::iterator it = overlays.begin(); it != overlays.end(); ++it) {
          if(*it) {
            if ((*it)->ai) delete ((*it)->ai);
            if ((*it)->tex) SDL_DestroyTexture((*it)->tex);
            delete (*it);
          }
        }
        overlays.clear();

        if(interrupted_redraw) {
            queueRerender();
        }
    }

#else
    fprintf(stderr, "mpegplay command is disabled.\n");
#endif


    return ret;
}


void PonscripterLabel::playAVI(const pstring& filename, bool click_flag)
{
#ifdef USE_AVIFILE
    pstring abs_fname = archive_path + filename;
    replace_ascii(abs_fname, '/', DELIMITER[0]);
    replace_ascii(abs_fname, '\\', DELIMITER[0]);

    if (audio_open_flag) Mix_CloseAudio();

    AVIWrapper* avi = new AVIWrapper();
    if (avi->init(abs_fname, false) == 0
        && avi->initAV(screen_surface, audio_open_flag) == 0) {
        if (avi->play(click_flag)) endCommand();
    }

    delete avi;

    if (audio_open_flag) {
        Mix_CloseAudio();
        openAudio();
    }

#else
    fprintf(stderr, "avi command is disabled.\n");
#endif
}


void PonscripterLabel::stopBGM(bool continue_flag)
{
#ifdef EXTERNAL_MIDI_PROGRAM
    FILE* com_file;
    if ((com_file = fopen("stop_bgm", "wb")) != NULL)
        fclose(com_file);

#endif

    if (mp3_sample) {
        SMPEG_stop(mp3_sample);
        Mix_HookMusic(NULL, NULL);
        SMPEG_delete(mp3_sample);
        mp3_sample = NULL;
    }

    if (music_struct.ovi){
        Mix_HaltMusic();
        Mix_HookMusic( NULL, NULL );
        closeOggVorbis(music_struct.ovi);
        music_struct.ovi = NULL;
    }

    if (wave_sample[MIX_BGM_CHANNEL]) {
        Mix_Pause(MIX_BGM_CHANNEL);
        Mix_FreeChunk(wave_sample[MIX_BGM_CHANNEL]);
        wave_sample[MIX_BGM_CHANNEL] = NULL;
    }

    if (!continue_flag) {
        music_file_name = "";
        music_play_loop_flag = false;
        if (music_buffer) {
            delete[] music_buffer;
            music_buffer = NULL;
        }
    }

    if (midi_info) {
#ifdef MACOSX
        if (timer_midi_id) {
            SDL_RemoveTimer(timer_midi_id);
            timer_midi_id = NULL;
        }

#endif

        ext_music_play_once_flag = true;
        Mix_HaltMusic();
        Mix_FreeMusic(midi_info);
        midi_info = NULL;
    }

    if (!continue_flag) {
        midi_file_name = "";
        midi_play_loop_flag = false;
    }

    if (music_info) {
        ext_music_play_once_flag = true;
        Mix_HaltMusic();
        Mix_FreeMusic(music_info);
        music_info = NULL;
    }
}


void PonscripterLabel::stopAllDWAVE()
{
    for (int ch = 0; ch < ONS_MIX_CHANNELS; ++ch) {
        if (wave_sample[ch]) {
            Mix_Pause(ch);
            Mix_FreeChunk(wave_sample[ch]);
            wave_sample[ch] = NULL;
        }
    }
}


void PonscripterLabel::playClickVoice()
{
    if (clickstr_state == CLICK_NEWPAGE) {
        if (clickvoice_file_name[CLICKVOICE_NEWPAGE].length() > 0)
            playSound(clickvoice_file_name[CLICKVOICE_NEWPAGE],
                      SOUND_WAVE | SOUND_OGG, false, MIX_WAVE_CHANNEL);
    }
    else if (clickstr_state == CLICK_WAIT) {
        if (clickvoice_file_name[CLICKVOICE_NORMAL].length() > 0)
            playSound(clickvoice_file_name[CLICKVOICE_NORMAL],
                      SOUND_WAVE | SOUND_OGG, false, MIX_WAVE_CHANNEL);
    }
}


void PonscripterLabel::setupWaveHeader(unsigned char *buffer, int channels,
                                       int rate, int bits,
                                       unsigned long data_length )
{
    memcpy(header.chunk_riff, "RIFF", 4);
    int riff_length = sizeof(WAVE_HEADER) + data_length - 8;
    header.riff_length[0] = riff_length & 0xff;
    header.riff_length[1] = (riff_length >> 8) & 0xff;
    header.riff_length[2] = (riff_length >> 16) & 0xff;
    header.riff_length[3] = (riff_length >> 24) & 0xff;
    memcpy(header.fmt_id, "WAVEfmt ", 8);
    header.fmt_size[0]  = 0x10;
    header.fmt_size[1]  = header.fmt_size[2] = header.fmt_size[3] = 0;
    header.data_fmt[0]  = 1; header.data_fmt[1] = 0; // PCM format
    header.channels[0]  = channels; header.channels[1] = 0;
    header.frequency[0] = rate & 0xff;
    header.frequency[1] = (rate >> 8) & 0xff;
    header.frequency[2] = (rate >> 16) & 0xff;
    header.frequency[3] = (rate >> 24) & 0xff;

    int sample_byte_size = channels * bits / 8;
    int byte_size = sample_byte_size * rate;
    header.byte_size[0] = byte_size & 0xff;
    header.byte_size[1] = (byte_size >> 8) & 0xff;
    header.byte_size[2] = (byte_size >> 16) & 0xff;
    header.byte_size[3] = (byte_size >> 24) & 0xff;
    header.sample_byte_size[0] = sample_byte_size;
    header.sample_byte_size[1] = 0;
    header.sample_bit_size[0] = bits;
    header.sample_bit_size[1]  = 0;

    memcpy(header.chunk_id, "data", 4);
    header.data_length[0] = (char) (data_length & 0xff);
    header.data_length[1] = (char) ((data_length >> 8) & 0xff);
    header.data_length[2] = (char) ((data_length >> 16) & 0xff);
    header.data_length[3] = (char) ((data_length >> 24) & 0xff);

    memcpy(buffer, &header, sizeof(header));
}


#ifdef USE_OGG_VORBIS
static size_t oc_read_func(void* ptr, size_t size, size_t nmemb,
			   void* datasource)
{
    OVInfo* ogg_vorbis_info = (OVInfo*) datasource;

    ogg_int64_t len = size * nmemb;
    if (ogg_vorbis_info->pos + len > ogg_vorbis_info->length)
        len = ogg_vorbis_info->length - ogg_vorbis_info->pos;

    memcpy(ptr, ogg_vorbis_info->buf + ogg_vorbis_info->pos, len);
    ogg_vorbis_info->pos += len;

    return len;
}


static int oc_seek_func(void* datasource, ogg_int64_t offset, int whence)
{
    OVInfo* ogg_vorbis_info = (OVInfo*) datasource;

    ogg_int64_t pos = 0;
    if (whence == 0)
        pos = offset;
    else if (whence == 1)
        pos = ogg_vorbis_info->pos + offset;
    else if (whence == 2)
        pos = ogg_vorbis_info->length + offset;

    if (pos < 0 || pos > ogg_vorbis_info->length) return -1;

    ogg_vorbis_info->pos = pos;

    return 0;
}


static int oc_close_func(void* datasource)
{
    return 0;
}


static long oc_tell_func(void* datasource)
{
    OVInfo* ogg_vorbis_info = (OVInfo*) datasource;

    return ogg_vorbis_info->pos;
}


#endif
OVInfo* PonscripterLabel::openOggVorbis(unsigned char* buf, long len,
                                        int &channels, int &rate)
{
    OVInfo* ovi = NULL;

#ifdef USE_OGG_VORBIS

    vorbis_comment *vc;
    int isLoopLength = 0, i;
    ogg_int64_t fullLength;
    ovi = new OVInfo();

    ovi->buf = buf;
    ovi->decoded_length = 0;
    ovi->length = len;
    ovi->pos = 0;
    ovi->loop         = -1;
    ovi->loop_start   = -1;
    ovi->loop_end     =  0;
    ovi->loop_len     =  0;

    ov_callbacks oc;
    oc.read_func  = oc_read_func;
    oc.seek_func  = oc_seek_func;
    oc.close_func = oc_close_func;
    oc.tell_func  = oc_tell_func;
    if (ov_open_callbacks(ovi, &ovi->ovf, NULL, 0, oc) < 0) {
        delete ovi;
        return NULL;
    }

    vorbis_info* vi = ov_info(&ovi->ovf, -1);
    if (vi == NULL) {
        ov_clear(&ovi->ovf);
        delete ovi;
        return NULL;
    }

    channels = vi->channels;
    ovi->channels = vi->channels;
    rate = vi->rate;

    /* vorbis loop start!! */

#if 0
    vc = ov_comment(&ovi->ovf, -1);
    for (i = 0; i < vc->comments; i++) {
        int   paramLen = vc->comment_lengths[i] + 1;
        char *param = (char *)SDL_malloc((size_t)paramLen);
        char *argument  = param;
        char *value     = param;
        SDL_memset(param, 0, (size_t)paramLen);
        SDL_memcpy(param, vc->user_comments[i], (size_t)vc->comment_lengths[i]);
        value = SDL_strchr(param, '=');
        if (value == NULL) {
            value = param + paramLen - 1; /* set null */
        } else {
            *(value++) = '\0';
        }

        #ifdef __USE_ISOC99
        #define A_TO_OGG64(x) (ogg_int64_t)atoll(x)
        #else
        #define A_TO_OGG64(x) (ogg_int64_t)atol(x)
        #endif

        if (SDL_strcasecmp(argument, "LOOPSTART") == 0)
            ovi->loop_start = A_TO_OGG64(value);
        else if (SDL_strcasecmp(argument, "LOOPLENGTH") == 0) {
            ovi->loop_len = A_TO_OGG64(value);
            isLoopLength = 1;
        }
        else if (SDL_strcasecmp(argument, "LOOPEND") == 0) {
            isLoopLength = 0;
            ovi->loop_end = A_TO_OGG64(value);
        }

        #undef A_TO_OGG64
        SDL_free(param);
    }
#endif

    if (isLoopLength == 1)
        ovi->loop_end = ovi->loop_start + ovi->loop_len;
    else
        ovi->loop_len = ovi->loop_end - ovi->loop_start;

    fullLength = ov_pcm_total(&ovi->ovf, -1);
    if (((ovi->loop_start >= 0) || (ovi->loop_end > 0)) &&
        ((ovi->loop_start < ovi->loop_end) || (ovi->loop_end == 0)) &&
         (ovi->loop_start < fullLength) &&
         (ovi->loop_end <= fullLength)) {
        if (ovi->loop_start < 0) ovi->loop_start = 0;
        if (ovi->loop_end == 0)  ovi->loop_end = fullLength;
        ovi->loop = 1;
    }
    /* vorbis loop ends!! */

    ovi->cvt.buf = NULL;
    ovi->cvt_len = 0;
    SDL_BuildAudioCVT(&ovi->cvt,
        AUDIO_S16, channels, rate,
        audio_format.format, audio_format.channels, audio_format.freq);
    ovi->mult1 = 10;
    ovi->mult2 = (int) (ovi->cvt.len_ratio * 10.0);

    ovi->decoded_length = ov_pcm_total(&ovi->ovf, -1) * channels * 2;
#endif

    return ovi;
}


int PonscripterLabel::closeOggVorbis(OVInfo* ovi)
{
    if (ovi->buf) {
        ovi->buf = NULL;
#ifdef USE_OGG_VORBIS
        ovi->length = 0;
        ovi->pos = 0;
        ov_clear(&ovi->ovf);
#endif
    }

    if (ovi->cvt.buf) {
        delete[] ovi->cvt.buf;
        ovi->cvt.buf = NULL;
        ovi->cvt_len = 0;
    }

    delete ovi;

    return 0;
}
