/* SPDX-License-Identifier: GPL-3.0-only */
/* Generated from the symbols file for OSOS 2.0.5. */
#pragma once

#define OSOS_VERSION "2.0.5"
#define OSOS_RUNTIME_BASE 0x08000000
#define OSOS_RUNTIME_FILE_OFFSET 0x0000b798
#define OSOS_NATIVE_HEAP_BASE 0x08b3b1b0

#ifndef __ASSEMBLER__
#include <stdint.h>

struct AudioEffectsContext;
struct DateTime;
struct DirectoryIterator;
struct EGLNativeWindow;
struct GameFileRequest;
struct GameImageFormat;
struct GameInputEvent;
struct GameUser;
struct MediaEntry;
struct MediaItem;
struct MediaList;
struct MediaTrack;
struct NativePath;
struct NativeString;
struct ThreeBandEQ;

int ArtworkDatabase_ReadImageThumbs(void *reader, void *image, uint32_t count,
                                    uint32_t flags);
void *ArtworkDatabase_FindById(void *database, uint32_t artwork_id);
void ArtworkThumb_Destroy(void *thumb);
void *ArtworkThumb_GetNameCache(void *thumb);
void EQ_LoadPresetCoefficients(struct ThreeBandEQ *eq);
void EQ_ResetAllBiquadStates(struct ThreeBandEQ *eq);
void EQ_ResetBiquadState(struct ThreeBandEQ *eq, uint32_t stage);
void EQ_ProcessBiquadStereoS16(struct ThreeBandEQ *eq, const int16_t *input,
                               int16_t *output, uint32_t frames, uint32_t stage);
void EQ_ProcessThreeBiquadsInPlace(struct ThreeBandEQ *eq, int16_t *input,
                                   uint32_t frames, int16_t **output,
                                   uint32_t *output_frames);
void EQ_SetDspPreset(struct ThreeBandEQ *eq, uint32_t preset);
void EQ_SetSampleRate(struct ThreeBandEQ *eq, uint32_t rate_hz);
struct ThreeBandEQ *EQ_InitThreeBiquadState(struct ThreeBandEQ *eq);
uint32_t EQ_SettingsIdToDspPreset(uint32_t settings_id);
int EQ_SettingsIdToMenuIndex(uint32_t settings_id, uint32_t *menu_index);
int EQ_MenuIndexToSettingsId(uint32_t menu_index, uint32_t *settings_id);
void MediaTrack_ApplyEQ(struct MediaTrack *track);
int MediaTrack_IsReady(struct MediaTrack *track);
void Audio_SetPlaybackSource(void *source);
void Audio_SetPostEffectsGainQ15(struct AudioEffectsContext *context,
                                 uint32_t gain_q15);
int Audio_SetProperty(struct AudioEffectsContext *context, int unused,
                      uint32_t property, const void *value);
int AudioService_SetProperty(void *service, int unused, uint32_t property,
                             const void *value);
void Audio_SetSpeed(struct AudioEffectsContext *context, const uint8_t *mode_and_value);
int Audio_SetSourceFormat(struct AudioEffectsContext *context, uint32_t rate_hz,
                          uint32_t bits, uint32_t channels, uint8_t flag);
void Audio_SetInputFrames(struct AudioEffectsContext *context, uint32_t frames);
int Audio_SetOutputFormat(struct AudioEffectsContext *context, uint32_t rate_hz,
                          uint32_t bits, uint32_t channels);
void Audio_GetOutputFrames(struct AudioEffectsContext *context, uint32_t *frames);
void Audio_SetEQPreset(struct AudioEffectsContext *audio, uint32_t preset);
void *AudioMixer_Get(void);
void AudioMixer_MixOutputBuffer(void *mixer, int16_t *pcm, uint32_t frames);
void AudioMixer_ScaleStereoQ15(void *mixer, int16_t *pcm, uint32_t left_q15,
                               uint32_t right_q15, uint32_t frames);
void AudioMixer_SetOutputLevel(void *mixer, uint32_t level, uint32_t channel);
void Audio_SetOutputLevel(uint32_t level, uint32_t channel);
void AudioMixer_SetSoundCheckGain(void *mixer, int16_t gain_db);
void *AudioOutputDevice_Get(void);
int AudioOutputDevice_SetSoundCheckGain(void *device, uint16_t gain_db);
int Audio_RepositionDecoder(void *stream, uint32_t position);
int AudioOutputQueue_Rebuild(void *queue, int keep_position, uint32_t position);
void *AudioOutputQueue_Get(void);
int AudioOutputQueue_Write(void *queue, uint32_t unused, const int16_t *pcm,
                           uint32_t frames, uint32_t unused2, uint32_t tag,
                           uint32_t flags);
void *AudioOutputQueue_Get_thunk(void);
int AudioOutputQueue_Write_thunk(void *queue, uint32_t unused, const int16_t *pcm,
                                 uint32_t frames, uint32_t unused2, uint32_t tag,
                                 uint32_t flags);
void MediaTrack_ApplyPlaybackSettings(struct MediaTrack *track);
void MediaTrack_ApplyVolumeAdjustment(struct MediaTrack *track);
void *Volume_Get(void);
void Volume_Apply(void *volume, uint32_t level, int notify);
int DisplayPipe_SetGammaTables(const uint32_t *red, const uint32_t *green,
                               const uint32_t *blue);
int DisplayPipe_Init(void *display);
int FairPlay_VerifyGameFileAndCreateContext(uint32_t storage, const void *data,
                                            uint32_t bytes, const uint8_t *digest,
                                            const void *optional, const void *sinf,
                                            uint32_t sinf_bytes, void **context);
int FairPlay_DecryptBuffer(void *context, const void *input, void *output,
                           uint32_t bytes);
int FairPlay_ReleaseContextAdapter(void *context);
void GameManifest_InitReader(void *reader, const char *manifest_path, uint8_t volume,
                             const char *signature_path);
int GameManifest_ReadWithOptionalSignature(void *reader, void *out_manifest);
int GameLoader_LoadVerifiedManifest(void *loader);
int GameLoader_ReadVerifyAndDecryptExecutable(void *loader, const char *identifier,
                                              uint32_t capacity, void *buffer,
                                              uint32_t *out_bytes);
int GameLoader_ResolveImportsAndEntryPoints(void *image, uint32_t image_bytes,
                                            uint32_t *out_entries);
void *GameMemory_Allocate(uint32_t byte_count);
void *GameMemory_Reallocate(void *block, uint32_t byte_count);
void GameMemory_Free(void *block);
void GameInput_ReadWheel(int32_t *out_delta, uint32_t *out_flags);
int32_t GameSettings_GetValue(const char *name, void *out_value, uint32_t *size);
int GameRuntime_RequestLaunch(void *runtime, struct NativeString *identifier,
                              uint8_t storage, uint32_t notification);
int GameRuntime_LoadManifestAndLaunch(void *runtime);
void GameRuntime_InvokeFrameCallback(void *runtime);
int GameRuntime_ShouldContinueFrames(void *runtime);
void GameRuntime_AppendInputEvent(void *runtime, uint8_t button, uint8_t type,
                                  uint32_t timestamp);
void GameFiles_BuildPath(struct NativeString *out, uint32_t directory_kind,
                         struct NativeString *identifier, uint32_t storage,
                         const char *relative_path);
const char *GameFiles_GetStorageDirectoryName(uint32_t directory_kind);
int GameCatalog_ReadManifestMetadata(struct NativeString *identifier, uint32_t storage,
                                     struct NativeString *out_name,
                                     struct NativeString *out_version,
                                     struct NativeString *out_user_data_path,
                                     struct NativePath *out_artwork_path,
                                     void *out_localized_names);
void *GameDirectory_BuildList(void *out_result, void *request);
void *GamesService_Get(void);
uint32_t GamesService_BuildList(void *service);
void GamesService_ScanRoot(void *service, void *list, uint32_t volume,
                           const char *root);
void GamesService_ClearList(void *service);
int GamesService_Init(void *service);
int GamesService_HandleEvent(void *service, void *event);
void *GameListEntry_Construct(void *entry, struct NativeString *name,
                              struct NativePath *empty_path,
                              struct NativeString *identifier,
                              struct NativePath *artwork_path,
                              struct NativeString *version,
                              struct NativeString *user_data_path,
                              void *localized_names, uint8_t storage);
void *GameListEntry_Destroy(void *entry);
int GameList_RemoveEntry(void *list, void *entry);
int GameList_RemoveAt(void *list, int index);
void *GameList_Destroy(void *list);
int GamesService_GetIndexedProperty(void *service, uint32_t type, uint32_t property,
                                    uint32_t index, void **out);
void GamesService_RequestSelectedGameLaunch(void *service);
int GamesService_GetArtworkId(void *service, uint32_t index, uint32_t *out_id);
int GamesService_GetArtworkPath(void *service, uint32_t artwork_id,
                                struct NativePath *out);
uint8_t GamesService_GetArtworkStorage(void *service, uint32_t artwork_id);
void GamesService_StopGame(void *service);
void GamesService_AbortGame(void *service);
void GamesService_ResetAfterGame(void *service);
int GameSystem_SetBacklightEnabled(int enabled);
int GameSystem_SetBacklightLevel(int percent);
int GameSystem_GetBacklightLevel(void);
void GameSystem_SetBacklightDimmed(int dimmed);
void GameSystem_Sleep(uint32_t milliseconds);
void GameSystem_GetDateTime(uint32_t out_fields[6]);
int GameSystem_GetBatteryLevel(void);
int GameSystem_IsExternalPowerConnected(void);
int GameSystem_GetPath(uint32_t directory_kind, char *out_path, int *size,
                       const char *relative_path);
int GameSystem_GetGameName(char *out_name, int capacity, int localized);
uint32_t GameSystem_GetGamesPlatformId(void);
int GameSystem_IsModelCode1(void);
void Clock_ReadMicroseconds(uint32_t *out_microseconds);
int GameDebug_OpenFile(uint32_t directory_kind, const char *relative_path,
                       int read_only, void **out_handle);
void GameDebug_CloseFile(void *handle);
int GameDebug_ReadFile(void *handle, void *buffer, uint32_t byte_count,
                       uint32_t *out_count);
int GameDebug_WriteFile(void *handle, const void *buffer, uint32_t byte_count,
                        uint32_t *out_count);
void GameDebug_Printf(const char *format, ...);
void GameInput_ForwardButtonEvent(const struct GameInputEvent *event);
int GameFile_QueueOpen(uint32_t directory_kind, const char *relative_path,
                       int read_only, struct GameFileRequest *request);
int GameFile_QueueClose(struct GameFileRequest *request);
int GameFile_QueueTransfer(struct GameFileRequest *request);
int GameFile_QueueReadFile(uint32_t directory_kind, const char *relative_path,
                           struct GameFileRequest *request);
int GameFile_QueueWriteFile(uint32_t directory_kind, const char *relative_path,
                            struct GameFileRequest *request);
int GameFile_QueueDelete(uint32_t directory_kind, const char *relative_path,
                         struct GameFileRequest *request);
int GameFile_QueueRename(uint32_t directory_kind, const char *directory,
                         const char *name, const char *new_name,
                         struct GameFileRequest *request);
int GameDirectory_QueueList(uint32_t directory_kind, const char *relative_path,
                            uint32_t create, struct GameFileRequest *request);
int GameDirectory_Rewind(int list_handle);
int GameDirectory_ReadEntry(int list_handle, char *out_name, uint32_t capacity,
                            uint8_t *out_is_directory);
void GameDirectory_ReleaseList(int list_handle);
int GameDirectory_GetStatus(int list_handle);
int GameSaveFile_Open(uint32_t directory_kind, const char *relative_path,
                      int *out_slot);
int GameSaveFile_Retain(uint32_t slot);
int GameSaveFile_Close(uint32_t slot);
int GameSaveFile_Read(uint32_t slot, void *buffer, int *byte_count, int whence,
                      uint32_t offset);
int GameSaveFile_Write(uint32_t slot, const void *buffer, int byte_count);
int GameSound_Create(void);
int GameSound_Destroy(int sound);
int GameSound_Play(int sound);
int GameSound_Pause(int sound);
int GameSound_Resume(int sound);
int GameSound_Stop(int sound);
int GameSound_Unused(int sound);
int GameSound_SetData(int sound, const void *samples);
int GameSound_SetByteCount(int sound, uint32_t byte_count);
int GameSound_SetFlagC(int sound, uint8_t value);
int GameSound_SetSampleRate(int sound, uint32_t hertz);
int GameSound_SetChannels(int sound, uint32_t channels);
int GameSound_SetBitsPerSample(int sound, uint32_t bits);
int GameSound_SetVolume(int sound, uint32_t volume);
int GameSound_SetPan(int sound, int32_t pan);
int GameSound_SetSpeed(int sound, uint32_t speed);
int GameSound_SetPlayCount(int sound, uint32_t count);
int GameSound_SetState(int sound, uint8_t state);
int GameSound_SetFlags(int sound, uint8_t flags);
int GameSound_SetTailMilliseconds(int sound, uint32_t milliseconds);
int GameSound_SetLeadInMilliseconds(int sound, uint32_t milliseconds);
int GameSound_SetNext(int sound, int next_sound);
int GameSound_SetMusicDuck(int sound, uint32_t level, uint32_t milliseconds);
const void *GameSound_GetData(int sound);
uint32_t GameSound_GetByteCount(int sound);
uint8_t GameSound_GetFlagC(int sound);
uint32_t GameSound_GetSampleRate(int sound);
uint32_t GameSound_GetChannels(int sound);
uint32_t GameSound_GetBitsPerSample(int sound);
uint32_t GameSound_GetVolume(int sound);
uint32_t GameSound_GetSpeed(int sound);
uint32_t GameSound_GetPlayCount(int sound);
int32_t GameSound_GetPan(int sound);
uint32_t GameSound_GetSpeedScale(void);
uint32_t GameSound_GetZero(void);
int GameSound_GetState(int sound);
int GameSound_GetFlags(int sound);
uint32_t GameSound_GetTailMilliseconds(int sound);
uint32_t GameSound_GetLeadInMilliseconds(int sound);
uint32_t GameSound_GetVoicePosition(int sound);
int GameSound_GetNext(int sound);
int GameSound_IsPlaying(int sound);
void glActiveTexture(uint32_t texture);
void glAlphaFunc(uint32_t func, float ref);
void glAlphaFuncx(uint32_t func, int32_t ref);
void glBindBuffer(uint32_t target, uint32_t buffer);
void glBindTexture(uint32_t target, uint32_t texture);
void glBlendFunc(uint32_t sfactor, uint32_t dfactor);
void glBufferData(uint32_t target, int32_t size, const void *data, uint32_t usage);
void glBufferSubData(uint32_t target, int32_t offset, int32_t size, const void *data);
void glClear(uint32_t mask);
void glClearColor(float red, float green, float blue, float alpha);
void glClearColorx(int32_t red, int32_t green, int32_t blue, int32_t alpha);
void glClearDepthf(float depth);
void glClearDepthx(int32_t depth);
void glClearStencil(int32_t s);
void glClientActiveTexture(uint32_t texture);
void glClipPlanex(uint32_t plane, const int32_t *equation);
void glClipPlanef(uint32_t plane, const float *equation);
void glColor4f(float red, float green, float blue, float alpha);
void glColor4x(int32_t red, int32_t green, int32_t blue, int32_t alpha);
void glColorMask(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha);
void glColorPointer(int32_t size, uint32_t type, int32_t stride, const void *pointer);
void glCompressedTexImage2D(uint32_t target, int32_t level, uint32_t internalformat,
                            int32_t width, int32_t height, int32_t border,
                            int32_t image_size, const void *data);
void glCompressedTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset,
                               int32_t yoffset, int32_t width, int32_t height,
                               uint32_t format, int32_t image_size, const void *data);
void glCopyTexImage2D(uint32_t target, int32_t level, uint32_t internalformat,
                      int32_t x, int32_t y, int32_t width, int32_t height,
                      int32_t border);
void glCopyTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset,
                         int32_t yoffset, int32_t x, int32_t y, int32_t width,
                         int32_t height);
void glCullFace(uint32_t mode);
void glDeleteBuffers(int32_t n, const uint32_t *buffers);
void glDeleteTextures(int32_t n, const uint32_t *textures);
void glDepthFunc(uint32_t func);
void glDepthMask(uint8_t flag);
void glDepthRangef(float near, float far);
void glDepthRangex(int32_t near, int32_t far);
void glDisable(uint32_t cap);
void glDisableClientState(uint32_t array);
void glDrawArrays(uint32_t mode, int32_t first, int32_t count);
void glDrawElements(uint32_t mode, int32_t count, uint32_t type, const void *indices);
void glEnable(uint32_t cap);
void glEnableClientState(uint32_t array);
void glFinish(void);
void glFlush(void);
void glFogf(uint32_t pname, float param);
void glFogfv(uint32_t pname, const float *params);
void glFogx(uint32_t pname, int32_t param);
void glFogxv(uint32_t pname, const int32_t *params);
void glFrontFace(uint32_t mode);
void glFrustumf(float left, float right, float bottom, float top, float near,
                float far);
void glFrustumx(int32_t left, int32_t right, int32_t bottom, int32_t top, int32_t near,
                int32_t far);
void glGetBooleanv(uint32_t pname, uint8_t *params);
void glGetBufferParameteriv(uint32_t target, uint32_t pname, int32_t *params);
void glGetClipPlanef(uint32_t plane, float *equation);
void glGetClipPlanex(uint32_t plane, int32_t *equation);
void glGenBuffers(int32_t n, uint32_t *buffers);
void glGenTextures(int32_t n, uint32_t *textures);
uint32_t glGetError(void);
void glGetFixedv(uint32_t pname, int32_t *params);
void glGetFloatv(uint32_t pname, float *params);
void glGetIntegerv(uint32_t pname, int32_t *params);
void glGetLightfv(uint32_t light, uint32_t pname, float *params);
void glGetLightxv(uint32_t light, uint32_t pname, int32_t *params);
void glGetMaterialfv(uint32_t face, uint32_t pname, float *params);
void glGetMaterialxv(uint32_t face, uint32_t pname, int32_t *params);
void glGetPointerv(uint32_t pname, void **params);
const uint8_t *glGetString(uint32_t name);
void glGetTexEnviv(uint32_t env, uint32_t pname, int32_t *params);
void glGetTexEnvfv(uint32_t env, uint32_t pname, float *params);
void glGetTexEnvxv(uint32_t env, uint32_t pname, int32_t *params);
void glGetTexParameteriv(uint32_t target, uint32_t pname, int32_t *params);
void glGetTexParameterfv(uint32_t target, uint32_t pname, float *params);
void glGetTexParameterxv(uint32_t target, uint32_t pname, int32_t *params);
void glHint(uint32_t target, uint32_t mode);
uint8_t glIsBuffer(uint32_t buffer);
uint8_t glIsEnabled(uint32_t cap);
uint8_t glIsTexture(uint32_t texture);
void glLightModelf(uint32_t pname, float param);
void glLightModelfv(uint32_t pname, const float *params);
void glLightModelx(uint32_t pname, int32_t param);
void glLightModelxv(uint32_t pname, const int32_t *params);
void glLightf(uint32_t light, uint32_t pname, float param);
void glLightfv(uint32_t light, uint32_t pname, const float *params);
void glLightx(uint32_t light, uint32_t pname, int32_t param);
void glLightxv(uint32_t light, uint32_t pname, const int32_t *params);
void glLineWidth(float width);
void glLineWidthx(int32_t width);
void glLoadIdentity(void);
void glLoadMatrixf(const float *m);
void glLoadMatrixx(const int32_t *m);
void glLogicOp(uint32_t opcode);
void glMaterialf(uint32_t face, uint32_t pname, float param);
void glMaterialfv(uint32_t face, uint32_t pname, const float *params);
void glMaterialx(uint32_t face, uint32_t pname, int32_t param);
void glMaterialxv(uint32_t face, uint32_t pname, const int32_t *params);
void glMatrixMode(uint32_t mode);
void glMultMatrixf(const float *m);
void glMultMatrixx(const int32_t *m);
void glMultiTexCoord4f(uint32_t target, float s, float t, float r, float q);
void glMultiTexCoord4x(uint32_t target, int32_t s, int32_t t, int32_t r, int32_t q);
void glNormal3f(float nx, float ny, float nz);
void glNormal3x(int32_t nx, int32_t ny, int32_t nz);
void glNormalPointer(uint32_t type, int32_t stride, const void *pointer);
void glOrthof(float left, float right, float bottom, float top, float near, float far);
void glOrthox(int32_t left, int32_t right, int32_t bottom, int32_t top, int32_t near,
              int32_t far);
void glPixelStorei(uint32_t pname, int32_t param);
void glPointParameterf(uint32_t pname, float param);
void glPointParameterfv(uint32_t pname, const float *params);
void glPointParameterx(uint32_t pname, int32_t param);
void glPointParameterxv(uint32_t pname, const int32_t *params);
void glPointSize(float size);
void glPointSizex(int32_t size);
void glPolygonOffset(float factor, float units);
void glPolygonOffsetx(int32_t factor, int32_t units);
void glPopMatrix(void);
void glPushMatrix(void);
void glReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format,
                  uint32_t type, void *pixels);
void glRotatef(float angle, float x, float y, float z);
void glRotatex(int32_t angle, int32_t x, int32_t y, int32_t z);
void glSampleCoverage(float value, uint8_t invert);
void glSampleCoveragex(int32_t value, uint8_t invert);
void glScalef(float x, float y, float z);
void glScalex(int32_t x, int32_t y, int32_t z);
void glScissor(int32_t x, int32_t y, int32_t width, int32_t height);
void glShadeModel(uint32_t mode);
void glStencilFunc(uint32_t func, int32_t ref, uint32_t mask);
void glStencilMask(uint32_t mask);
void glStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass);
void glTexCoordPointer(int32_t size, uint32_t type, int32_t stride,
                       const void *pointer);
void glTexEnvf(uint32_t target, uint32_t pname, float param);
void glTexEnvfv(uint32_t target, uint32_t pname, const float *params);
void glTexEnvx(uint32_t target, uint32_t pname, int32_t param);
void glTexEnvxv(uint32_t target, uint32_t pname, const int32_t *params);
void glTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width,
                  int32_t height, int32_t border, uint32_t format, uint32_t type,
                  const void *pixels);
void glTexParameteri(uint32_t target, uint32_t pname, int32_t param);
void glTexParameterf(uint32_t target, uint32_t pname, float param);
void glTexParameterx(uint32_t target, uint32_t pname, int32_t param);
void glTexSubImage2D(uint32_t target, int32_t level, int32_t xoffset, int32_t yoffset,
                     int32_t width, int32_t height, uint32_t format, uint32_t type,
                     const void *pixels);
void glTranslatef(float x, float y, float z);
void glTranslatex(int32_t x, int32_t y, int32_t z);
void glVertexPointer(int32_t size, uint32_t type, int32_t stride, const void *pointer);
void glViewport(int32_t x, int32_t y, int32_t width, int32_t height);
uint32_t glQueryMatrixxOES(int32_t *mantissa, int32_t *exponent);
uint32_t eglGetError(void);
void *eglGetDisplay(void *display_id);
uint32_t eglInitialize(void *display, int32_t *major, int32_t *minor);
uint32_t eglTerminate(void *display);
const char *eglQueryString(void *display, int32_t name);
uint32_t eglGetConfigs(void *display, void **configs, int32_t config_size,
                       int32_t *num_config);
uint32_t eglChooseConfig(void *display, const int32_t *attrib_list, void **configs,
                         int32_t config_size, int32_t *num_config);
uint32_t eglGetConfigAttrib(void *display, void *config, int32_t attribute,
                            int32_t *value);
void *eglCreateWindowSurface(void *display, void *config,
                             const struct EGLNativeWindow *window,
                             const int32_t *attrib_list);
void *eglCreatePixmapSurface(void *display, void *config,
                             const struct EGLNativeWindow *pixmap,
                             const int32_t *attrib_list);
void *eglCreatePbufferSurface(void *display, void *config, const int32_t *attrib_list);
uint32_t eglDestroySurface(void *display, void *surface);
uint32_t eglQuerySurface(void *display, void *surface, int32_t attribute,
                         int32_t *value);
uint32_t eglSurfaceAttrib(void *display, void *surface, int32_t attribute,
                          int32_t value);
uint32_t eglBindTexImage(void *display, void *surface, int32_t buffer);
uint32_t eglReleaseTexImage(void *display, void *surface, int32_t buffer);
uint32_t eglSwapInterval(void *display, int32_t interval);
void *eglCreateContext(void *display, void *config, void *share_context,
                       const int32_t *attrib_list);
uint32_t eglDestroyContext(void *display, void *context);
uint32_t eglMakeCurrent(void *display, void *draw, void *read, void *context);
void *eglGetCurrentContext(void);
void *eglGetCurrentSurface(int32_t readdraw);
void *eglGetCurrentDisplay(void);
uint32_t eglQueryContext(void *display, void *context, int32_t attribute,
                         int32_t *value);
uint32_t eglWaitGL(void);
uint32_t eglWaitNative(int32_t engine);
uint32_t eglSwapBuffers(void *display, void *surface);
uint32_t eglCopyBuffers(void *display, void *surface, void *target);
void EGL_StoreSurfaceWord(void *surface, uint32_t value);
void glRegisterBinaryShaderAPPLE(uint32_t kind, const void *shader);
void glUnregisterBinaryShaderAPPLE(uint32_t kind);
uint32_t GameAudio_GetRepeatMode(void);
void GameAudio_SetRepeatMode(uint8_t mode);
uint32_t GameAudio_GetPlaybackOrder(void);
void GameAudio_SetPlaybackOrder(uint8_t order);
void GameAudio_RefreshCurrentEntry(void);
void GameAudio_SetShuffleOrderBuilt(uint8_t built);
void GameAudio_SetEQMenuIndexTemporary(uint32_t index);
void GameAudio_SetEQMenuIndex(uint32_t index);
uint32_t GameAudio_GetEQMenuIndex(void);
int GameAudio_SetSpeed(int speed);
int GameAudio_GetSpeed(void);
void GameAudio_SetGain(int32_t gain);
int32_t GameAudio_GetGain(void);
void GameAudio_InitPlaybackList(uint8_t shuffle, uint8_t repeat);
void GameAudio_AppendMediaSelection(const char *path, void *record, uint8_t use_record,
                                    uint32_t duration_ms);
void GameAudio_RemoveEntryAtIndex(uint32_t index);
void GameAudio_ClearPlaybackList(void);
void GameAudio_SetPlaybackChanged(uint8_t changed);
void GameAudio_PlayIndex(uint32_t index, uint8_t flag, uint32_t option);
void GameAudio_Stop(void);
void GameAudio_TogglePlayPause(uint8_t wait);
void GameAudio_StartScanForward(void);
void GameAudio_StartScanBackward(void);
void GameAudio_StopScan(void);
void GameAudio_Seek(uint32_t milliseconds);
void GameAudio_SetItemStartPosition(uint32_t index, uint32_t position_ms);
void GameAudio_NextTrack(void);
void GameAudio_PreviousTrack(void);
uint32_t GameAudio_GetCurrentIndex(void);
uint32_t GameAudio_GetEntryCount(void);
int32_t GameAudio_GetItemHandleAtIndex(uint32_t index);
uint32_t GameAudio_GetPosition(void);
uint32_t GameAudio_GetRemainingPosition(void);
uint8_t GameAudio_GetPlaybackState(void);
int GameAudio_IsPlaying(void);
int GameAudio_IsPaused(void);
int GameAudio_IsStopped(void);
int GameAudio_IsScanningForward(void);
int GameAudio_IsScanningBackward(void);
int GameAudio_IsScanning(void);
int GameAudio_IsShuffleEnabled(void);
int GameAudio_IsRepeatOne(void);
int GameAudio_IsRepeatAll(void);
int GameAudio_IsPlaybackOrderReversed(void);
uint8_t GameAudio_IsPlayingOnTheGo(void);
uint8_t GameAudio_IsPlaybackListUnrestricted(void);
int GameAudio_HasPlaybackList(void);
uint32_t GameAudio_GetVolume(void);
uint32_t GameAudio_GetVolumeLimit(void);
void GameAudio_SetVolume(uint32_t volume);
uint32_t GameAudio_GetMixerLevel(void);
void GameAudio_SetMixerLevel(uint32_t level);
void GameAudio_GetVersion(uint16_t out_version[2]);
void GameAudio_RequestPowerHold(void);
int32_t GameMetadata_PhotoLibraryCreate(void);
void GameMetadata_PhotoLibraryDestroy(int32_t photo_library);
uint32_t GameMetadata_PhotoLibraryGetAlbumCount(int32_t photo_library);
uint32_t GameMetadata_PhotoLibraryGetImageCount(int32_t photo_library);
uint32_t GameMetadata_PhotoLibraryGetFormatCount(int32_t photo_library);
int32_t GameMetadata_PhotoLibraryFindAlbum(int32_t photo_library, const char *name);
int32_t GameMetadata_PhotoLibraryGetAlbumAt(int32_t photo_library, uint32_t index);
int32_t GameMetadata_PhotoLibraryGetImageAt(int32_t photo_library, uint32_t index);
int32_t GameMetadata_PhotoLibraryGetImageById(int32_t photo_library,
                                              uint32_t artwork_id);
int32_t GameMetadata_PhotoLibraryGetFormatIdAt(int32_t photo_library, uint32_t index);
void GameMetadata_PhotoLibraryGetFormat(struct GameImageFormat *out,
                                        int32_t photo_library, int16_t format_id);
void GameMetadata_PhotoAlbumDestroy(int32_t album);
int GameMetadata_PhotoAlbumIsValid(int32_t album);
void GameMetadata_PhotoAlbumCopyName(int32_t album, char *buffer,
                                     uint32_t *inout_bytes);
uint32_t GameMetadata_PhotoAlbumGetImageCount(int32_t album);
int32_t GameMetadata_PhotoAlbumGetImageAt(int32_t album, uint32_t index);
uint8_t GameMetadata_PhotoAlbumGetByte27(int32_t album);
int32_t GameMetadata_PhotoAlbumGetPlaylist(int32_t album);
int32_t GameMetadata_PhotoAlbumGetTrack(int32_t album);
int32_t GameMetadata_ArtworkLibraryCreate(void);
void GameMetadata_ArtworkLibraryDestroy(int32_t artwork_library);
uint32_t GameMetadata_ArtworkLibraryGetFormatCount(int32_t artwork_library);
int32_t GameMetadata_ArtworkLibraryGetFormatIdAt(int32_t artwork_library,
                                                 uint32_t index);
void GameMetadata_ArtworkLibraryGetFormat(struct GameImageFormat *out,
                                          int32_t artwork_library, int16_t format_id);
int32_t GameMetadata_ArtworkLibraryGetTrackImage(int32_t artwork_library,
                                                 uint64_t persistent_id);
void GameMetadata_ImageDestroy(int32_t image);
int GameMetadata_ImageIsValid(int32_t image);
uint32_t GameMetadata_ImageGetId(int32_t image);
void GameMetadata_ImageGetDate(struct DateTime *out, int32_t image);
int GameMetadata_ImageIsSame(int32_t image, int32_t other);
int32_t GameMetadata_ImageCountThumbs(int32_t image, int16_t format_id);
int32_t GameMetadata_ImageGetThumbAt(int32_t image, int16_t format_id, int load,
                                     uint32_t index);
int32_t GameMetadata_ImageGetThumbBeforeAbove(int32_t image, int16_t format_id,
                                              int load, uint32_t limit);
void GameMetadata_ImageDataDestroy(int32_t image_data);
void GameMetadata_ImageDataLoad(int32_t image_data);
uint32_t GameMetadata_ImageDataGetDimensions(int32_t image_data);
uint32_t GameMetadata_ImageDataReadDimensions(int32_t image_data);
const void *GameMetadata_ImageDataGetLoadedHeader(int32_t image_data);
void GameMetadata_ImageDataGetFormat(struct GameImageFormat *out, int32_t image_data);
const void *GameMetadata_ImageDataGetPixels(int32_t image_data);
uint32_t GameMetadata_ImageDataGetByteCount(int32_t image_data);
int GameMetadata_ImageDataGetFileExtent(int32_t image_data, const char *scratch_path,
                                        uint32_t *out_offset, uint32_t *out_bytes);
int32_t GameMetadata_FilterCreate(const char *name);
void GameMetadata_FilterDestroy(int32_t filter);
void GameMetadata_FilterCopyName(int32_t filter, char *buffer, uint32_t *inout_bytes);
int32_t GameMetadata_LibraryCreate(void);
void GameMetadata_LibraryDestroy(int32_t library);
void GameMetadata_LibraryReset(int32_t library);
void GameMetadata_LibrarySelectPlaylist(int32_t library, int32_t playlist);
void GameMetadata_LibrarySelectArtist(int32_t library, int32_t filter);
void GameMetadata_LibrarySelectAlbum(int32_t library, int32_t filter);
void GameMetadata_LibrarySelectGenre(int32_t library, int32_t filter);
void GameMetadata_LibrarySelectComposer(int32_t library, int32_t filter);
void GameMetadata_LibrarySelectTVShow(int32_t library, int32_t filter);
void GameMetadata_LibrarySelectSeasonAt(int32_t library, uint32_t index);
void GameMetadata_LibrarySetBrowserFlagEF9(int32_t library);
void GameMetadata_LibrarySetBrowserFlagEF8(int32_t library);
void GameMetadata_LibrarySetKindMasks(int32_t library, uint32_t include_kinds,
                                      uint32_t exclude_kinds);
uint32_t GameMetadata_LibraryGetAlbumCount(int32_t library);
uint32_t GameMetadata_LibraryGetArtistCount(int32_t library);
uint32_t GameMetadata_LibraryGetGenreCount(int32_t library);
uint32_t GameMetadata_LibraryGetPlaylistCount(int32_t library);
uint32_t GameMetadata_LibraryGetComposerCount(int32_t library);
uint32_t GameMetadata_LibraryGetTrackCount(int32_t library);
uint32_t GameMetadata_LibraryGetTVShowCount(int32_t library);
uint32_t GameMetadata_LibraryGetSeasonCount(int32_t library);
uint32_t GameMetadata_LibraryGetFilteredPlaylistCount(int32_t library);
void GameMetadata_LibrarySetOrderMode1(int32_t library);
void GameMetadata_LibrarySetOrderMode3(int32_t library);
void GameMetadata_LibrarySetOrderMode0(int32_t library);
void GameMetadata_LibrarySetOrderMode9(int32_t library);
void GameMetadata_LibrarySetOrderMode2(int32_t library);
void GameMetadata_LibrarySetOrderMode4(int32_t library);
void GameMetadata_LibrarySetOrderMode5(int32_t library);
void GameMetadata_LibrarySetOrderMode8(int32_t library);
void GameMetadata_LibrarySetOrderMode7(int32_t library);
void GameMetadata_LibrarySetOrderFlag(int32_t library, uint8_t flag);
int GameMetadata_LibraryGetOrderFlag(int32_t library);
int32_t GameMetadata_LibraryGetTrackAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryFindTrack(int32_t library, uint64_t persistent_id);
int32_t GameMetadata_LibraryGetArtistAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetAlbumAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetGenreAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetComposerAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetTVShowAt(int32_t library, uint32_t index);
uint32_t GameMetadata_LibraryGetSeasonNumberAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetAlbumImage(int32_t library, uint32_t index);
void GameMetadata_LibraryClearSelections(int32_t library);
int32_t GameMetadata_LibraryGetFilteredPlaylistAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryGetPlaylistAt(int32_t library, uint32_t index);
int32_t GameMetadata_LibraryFindPlaylist(int32_t library, uint64_t persistent_id);
uint32_t GameMetadata_LibraryGetConstant(int32_t library);
void GameMetadata_LibraryCopyTableString(int32_t library, uint32_t index, char *buffer,
                                         uint32_t *inout_bytes);
uint8_t GameMetadata_LibraryGetTrackDataByte905(int32_t library);
uint8_t GameMetadata_LibraryGetTrackDataByte906(int32_t library);
void GameMetadata_TrackDestroy(int32_t track);
int GameMetadata_TrackIsValid(int32_t track);
uint64_t GameMetadata_TrackGetPersistentId(int32_t track);
int GameMetadata_TrackGetEntryId(int32_t track, uint32_t *out_id);
int GameMetadata_TrackGetEntryWord18(int32_t track, uint32_t *out_value);
void GameMetadata_TrackCopyPath(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyTitle(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyAlbum(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyArtist(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyGenre(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyComposer(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyGrouping(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyFileType(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyComment(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyCategory(int32_t track, char *buffer, uint32_t *inout_bytes);
void GameMetadata_TrackCopyDescription(int32_t track, char *buffer,
                                       uint32_t *inout_bytes);
void GameMetadata_TrackCopyShowName(int32_t track, char *buffer, uint32_t *inout_bytes);
uint32_t GameMetadata_TrackGetSeasonNumber(int32_t track);
void GameMetadata_TrackCopyPodcastRssUrl(int32_t track, char *buffer,
                                         uint32_t *inout_bytes);
void GameMetadata_TrackCopyDisplayArtist(int32_t track, char *buffer,
                                         uint32_t *inout_bytes);
void GameMetadata_TrackCopyRecordString48(int32_t track, char *buffer,
                                          uint32_t *inout_bytes);
uint32_t GameMetadata_TrackGetBookmarkMs(int32_t track);
uint32_t GameMetadata_TrackGetStartTimeMs(int32_t track);
uint32_t GameMetadata_TrackGetStopTimeMs(int32_t track);
uint32_t GameMetadata_TrackGetDurationSeconds(int32_t track);
uint32_t GameMetadata_TrackGetDurationMs(int32_t track);
uint32_t GameMetadata_TrackGetFileSizeBytes(int32_t track);
uint16_t GameMetadata_TrackGetBitrateKbps(int32_t track);
uint16_t GameMetadata_TrackGetYear(int32_t track);
int32_t GameMetadata_TrackGetRating(int32_t track);
uint16_t GameMetadata_TrackGetTrackNumber(int32_t track);
uint16_t GameMetadata_TrackGetDiscNumber(int32_t track);
uint32_t GameMetadata_TrackGetPlayCount(int32_t track);
uint32_t GameMetadata_TrackGetSkipCount(int32_t track);
uint16_t GameMetadata_TrackGetEQSettingsId(int32_t track);
float GameMetadata_TrackGetVolumeAdjustment(int32_t track);
int GameMetadata_TrackHasRecordFlag1C40(int32_t track);
int GameMetadata_TrackRemembersPosition(int32_t track);
int GameMetadata_TrackHasRecordFlag8F10(int32_t track);
int GameMetadata_TrackIsAudiobook(int32_t track);
int GameMetadata_TrackIsMP4Audio(int32_t track);
int GameMetadata_TrackIsM4A(int32_t track);
int GameMetadata_TrackIsM4B(int32_t track);
int GameMetadata_TrackIsM4P(int32_t track);
int GameMetadata_TrackIsMP3(int32_t track);
int GameMetadata_TrackIsWAV(int32_t track);
int GameMetadata_TrackIsAIFF(int32_t track);
int GameMetadata_TrackHasRecordFlag8F01(int32_t track);
int GameMetadata_TrackIsVideo(int32_t track);
int GameMetadata_TrackIsTVShow(int32_t track);
int GameMetadata_TrackIsMusicVideo(int32_t track);
int GameMetadata_TrackGetReleaseDate(int32_t track, struct DateTime *out);
void GameMetadata_TrackSetBookmarkMs(int32_t track, uint32_t bookmark_ms);
void GameMetadata_TrackSetRating(int32_t track, int32_t rating);
void GameMetadata_TrackIncrementPlayCount(int32_t track);
void GameMetadata_TrackIncrementSkipCount(int32_t track);
void GameMetadata_TrackGetLastPlayed(struct DateTime *out, int32_t track);
void GameMetadata_TrackGetDateAdded(struct DateTime *out, int32_t track);
void GameMetadata_TrackGetDateModified(struct DateTime *out, int32_t track);
void GameMetadata_TrackGetLastSkipped(struct DateTime *out, int32_t track);
uint32_t GameMetadata_TrackGetRecordWordF4(int32_t track);
uint32_t GameMetadata_TrackGetPregap(int32_t track);
uint32_t GameMetadata_TrackGetPostgap(int32_t track);
uint64_t GameMetadata_TrackGetSampleCount(int32_t track);
uint64_t GameMetadata_TrackGetGaplessData(int32_t track);
void GameMetadata_PlaylistDestroy(int32_t playlist);
int GameMetadata_PlaylistIsValid(int32_t playlist);
uint64_t GameMetadata_PlaylistGetPersistentId(int32_t playlist);
uint16_t GameMetadata_PlaylistGetCount(int32_t playlist);
void GameMetadata_PlaylistCopyName(int32_t playlist, char *buffer,
                                   uint32_t *inout_bytes);
int GameMetadata_PlaylistIsLibrary(int32_t playlist);
int32_t GameMetadata_PlaylistGetTrackAt(int32_t playlist, uint32_t index,
                                        uint32_t order_mode);
int GameMetadata_PlaylistRemoveEntryAt(int32_t playlist, uint32_t index,
                                       uint32_t order_mode, int reverse);
int GameMetadata_PlaylistRefresh(int32_t playlist);
uint32_t GameUser_GetCount(void);
struct GameUser *GameUser_GetAt(uint32_t index);
struct GameUser *GameUser_FindById(uint32_t id);
void GameUser_Remove(struct GameUser *user);
struct GameUser *GameUser_Add(const char *name);
const char *GameUser_GetName(struct GameUser *user);
void GameUser_CopyDateA(void *out_date, struct GameUser *user);
void GameUser_CopyDateB(void *out_date, struct GameUser *user);
uint16_t GameUser_GetId(struct GameUser *user);
void GameUser_SetName(struct GameUser *user, const char *name);
uint32_t eglSaveSurfaceHM(void *surface);
void glPointSizePointerOES(uint32_t type, int32_t stride, const void *pointer);
int32_t GameSettings_ReadMappedString(int32_t preference, void *out_value,
                                      uint32_t *size, const void *table);
int32_t GameSettings_ReadLanguageIndex(int32_t preference, void *out_value,
                                       uint32_t *size, const void *argument);
void *GameBacklight_Get(void);
void GameBacklight_Teardown(void);
int GameBacklight_SetLevel(void *backlight, int percent);
void GameBacklight_SetDimmed(void *backlight, int dimmed);
int GameSaveTable_Close(void *table, uint32_t slot);
struct GameUser *GameUserList_Add(void *users, const char *name);
int GameUserList_Transfer(void *users, int save);
void *GameMetadata_GetTrackers(void);
void GameMetadata_DestroyTrackers(void);
int GameAudio_GetCachedSpeed(void *bridge);
void GameAudio_QueuePreviousTrack(void *bridge);
uint8_t GameAudio_ReadPlayingOnTheGo(void *bridge);
uint8_t GameAudio_ReadPlaybackListUnrestricted(void *bridge);
uint32_t GameAudio_ReadMixerLevel(void);
int GameAudio_SnapshotPlayerSettings(void *bridge);
void GameAudio_RestorePlayerSettings(void *bridge);
void GameAudio_ApplyInitPlaybackList(void *bridge, uint32_t shuffle, uint32_t repeat);
void GameAudio_ApplyMediaSelection(void *bridge, void *owned_path, void *record,
                                   uint8_t use_record, uint32_t duration_ms);
void GameAudio_ApplyRemoveEntryAtIndex(void *bridge, uint32_t index);
void GameAudio_ApplyClearPlaybackList(void *bridge);
void GameAudio_ApplyPlaybackChanged(void *bridge, uint8_t changed);
uint32_t AudioMixer_GetMusicLevel(void);
void GameFileService_TaskEntry(void *service);
void GameFileService_RunWorker(void *service);
void GameFileService_Perform(void *service, void *entry);
void GameFileService_Append(void *service, void *entry);
void GameFileService_Drain(void *service);
void GameFileService_PerformOpen(void *service, void *entry);
void GameFileService_PerformClose(void *service, void *entry);
void GameFileService_PerformRead(void *service, void *entry);
void GameFileService_PerformWrite(void *service, void *entry);
void GameFileService_PerformSeek(void *service, void *entry);
void GameFileService_PerformDelete(void *service, void *entry);
void GameFileService_PerformRename(void *service, void *entry);
void GameFileService_PerformList(void *service, void *entry);
int GameFileService_AddHandle(void *service, void **handle);
void *GameFileService_GetHandle(void *service, int index);
void GameFileService_RemoveHandle(void *service, int index);
void GameRuntime_AppendCompletedRequest(void *runtime, struct GameFileRequest *request);
struct GameFileRequest *GameRuntime_TakeCompletedRequests(void *runtime);
void *GameSaveTable_Get(void);
int GameSaveTable_Open(void *table, uint32_t directory_kind, const char *relative_path,
                       int *out_slot);
int GameSaveTable_Retain(void *table, uint32_t slot);
int GameSaveTable_Read(void *table, uint32_t slot, void *buffer, int *byte_count,
                       int whence, uint32_t offset);
int GameSaveTable_Write(void *table, uint32_t slot, const void *buffer, int byte_count);
void GameSaveTable_FlushAll(void *table);
int GameSaveSlot_IsBound(void *slot);
int GameSaveSlot_Read(void *slot, void *buffer, int *byte_count, int whence,
                      uint32_t offset);
int GameSaveSlot_Store(void *slot, const void *buffer, uint32_t byte_count);
void GameSaveSlot_Release(void *slot);
void GameSaveSlot_Flush(void *slot);
void GameSaveSlot_Free(void *slot);
void Bitmap_LoadResource(void *bitmap, const void *resource, uint32_t argument,
                         uint32_t flags);
void *GraphicsContext_ConstructDefault(void *context);
void GraphicsContext_FillRect(void *context, const int32_t *rect);
void GraphicsContext_ClearRect(void *context, const int32_t *rect);
void GraphicsContext_SetColor(void *context, const uint8_t *rgba);
void GraphicsContext_SetBackgroundColor(void *context, const uint8_t *rgba);
void GraphicsContext_SetFont(void *context, const uint32_t *font);
void GraphicsContext_SetTextPen(void *context, int x, int baseline);
void GraphicsContext_SetDrawMode(void *context, uint8_t mode);
void GraphicsContext_DrawUtf8(void *context, const char *text);
int GraphicsContext_MeasureUtf8(void *context, const char *text);
void GraphicsContext_CopyRect(void *context, void *source, const int32_t *source_rect,
                              const int32_t *rect, uint32_t alpha, uint32_t flags);
void *GraphicsContext_InitWithBitmap(void *context, void *bitmap);
void *GraphicsContext_InitFromBitmapResource(void *context, void *bitmap,
                                             const void *resource);
void GraphicsContext_SetOrigin(void *context, int x, int y);
void *Bitmap_Construct(void *bitmap);
void *Bitmap_Destroy(void *bitmap);
void GraphicsContext_Destroy(void *context);
void Font_Construct(uint32_t *font, const char *family, uint32_t size, uint8_t style);
int GraphicsContext_GetFontAscent(void *context);
int GraphicsContext_GetFontDescent(void *context);
void Surface_FillRect(void *surface, const int32_t *rect, const uint8_t *rgba,
                      uint32_t mode, void *clip);
uint32_t RGB565_BlendFixed(uint32_t first, uint32_t second, int weight);
void *BitmapCache_Init(void *cache, void *service, uint32_t resource_id);
void BitmapCache_Load(void *cache);
void BitmapCache_GetRect(int32_t *rect, void *cache);
void BitmapCache_Draw(void *cache, void *context, const int32_t *source_rect,
                      const int32_t *rect, uint32_t alpha);
void Rect_Offset(int32_t *rect, int dx, int dy);
void Rect_Intersect(int32_t *rect, const int32_t *other);
void ArtworkRenderer_Draw(void *renderer);
void CoverFlowEdges_SmoothCenter(const void *buffer);
void GameGraphics_GetCurrentPixelBuffer(void **out_pixels);
int IAP_VerifyDeviceSignatureRSA(void *manager, uint32_t channel, uint32_t key_id,
                                 void *signature, uint32_t signature_bytes);
int IAP_VerifyDeviceSignatureAlternate(void *authentication, void *signature,
                                       uint32_t signature_bytes);
int Wheel_DispatchButtonChanges(int force_center);
void Input_HandleButton(uint32_t button, uint32_t pressed);
void Event_Signal(uint32_t event);
void IRQ_RestoreState(uint32_t cpsr);
uint32_t IRQ_DisableSave(void);
uint32_t InputWheel_ConvertPosition(uint32_t touch);
void InputWheel_ConsumeMotion(int32_t *out_delta, uint32_t *out_flags,
                              uint32_t *out_touch_count);
void InputWheel_ConsumeMotionAndExtra(int32_t *out_delta, uint32_t *out_flags,
                                      uint32_t *out_touch_count);
int EventManager_PostMessage(const void *message, int priority_queue);
void *EventManager_AllocEvent(void);
void EventManager_FreeEvent(void *event);
uint32_t EventManager_CountFreeEvents(void);
int MediaListModel_GetIndexedProperty(void *model, uint32_t type, uint32_t property,
                                      int row, uintptr_t *out);
void *MediaListModel_Destroy(void *model);
uint32_t MediaPlayer_GetPosition(void *player);
void MediaPlayer_InitializePlaybackList(void *player, uint32_t shuffle,
                                        uint32_t repeat);
int MediaPlayer_IsStopped(void *player);
void MediaPlayer_TogglePlayback(void *player);
void *MediaPlayer_GetInstance(void);
int MediaPlayer_ClearPreparedTrack(void *player);
int MediaPlayer_RebuildArtwork(void *player);
int MediaPlayer_GetCurrentItem(void *player, struct MediaItem *item);
int MediaPlayer_GetItemAtIndex(void *player, uint32_t index, struct MediaItem *item);
void MediaPlayer_SetCurrentIndex(void *player, int index, int restart);
int MediaPlayer_PlayIndex(void *player, int index, uint8_t flag, uint32_t start_ms,
                          uint32_t param_5);
void MediaPlayer_RemoveEntryAtIndex(void *player, uint32_t index);
int MediaPlayer_IsRepeatAll(void *player);
int MediaPlayer_IsRepeatOne(void *player);
int MediaPlayer_Seek(void *player, uint32_t position_ms);
void MediaPlayer_ApplyEQSettingsId(void *player, uint8_t settings_id);
uint8_t MediaPlayer_GetEQSettingsId(void *player);
uint32_t MediaPlayer_GetEQMenuIndex(void *player);
void MediaPlayer_Stop(void *player);
void MediaPlayer_ResetPlaybackList(void *player);
void App_SyncCurrentItem(void *app);
void *NowPlayingModel_Get(void);
int NowPlayingModel_RefreshCurrentItem(void *model);
int NowPlayingModel_GetProperty(void *model, uint32_t type, uint32_t property,
                                uintptr_t *out);
void MediaListController_PlayRow(void *controller, int row, int mode, uint32_t shuffle);
int AudiobookChapters_SelectRow(void *controller, int row);
int MediaPlayer_CreatePlaybackList(void *player, void *browse_context, void **out_list);
int MediaItem_IsValid(struct MediaItem *item);
struct MediaItem *MediaItem_InitFromRecord(struct MediaItem *item, void *record,
                                           uint8_t flags);
void MediaItem_Destroy(struct MediaItem *item);
void MediaItem_Assign(struct MediaItem *destination, const struct MediaItem *source);
uint32_t MediaItem_HasRecordFlag8F01(struct MediaItem *item);
uint32_t MediaItem_HasKind8(struct MediaItem *item);
uint32_t MediaItem_HasKind8062(struct MediaItem *item);
uint32_t MediaItem_HasKind200000(struct MediaItem *item);
void MediaItem_GetPath(struct NativeString *out, struct MediaItem *item);
uint32_t MediaItem_GetFileSizeBytes(struct MediaItem *item);
uint16_t MediaItem_GetBitrateKbps(struct MediaItem *item);
uint16_t MediaItem_GetYear(struct MediaItem *item);
int32_t MediaItem_GetRating(struct MediaItem *item);
uint16_t MediaItem_GetTrackNumber(struct MediaItem *item);
uint16_t MediaItem_GetDiscNumber(struct MediaItem *item);
uint32_t MediaItem_GetPlayCount(struct MediaItem *item);
uint32_t MediaItem_GetSkipCount(struct MediaItem *item);
uint32_t MediaItem_GetChapterCount(struct MediaItem *item);
uint32_t MediaItem_GetChapterStartMs(struct MediaItem *item, uint32_t index);
int MediaItem_GetChapterTitleAt(struct MediaItem *item, int position_ms,
                                struct NativeString *out);
uint16_t MediaItem_GetEQSettingsId(struct MediaItem *item);
int MediaItem_GetChapterIndexAt(struct MediaItem *item, uint32_t position_ms);
void MediaItem_GetFileType(struct MediaItem *item, struct NativeString *out);
void MediaItem_GetComment(struct MediaItem *item, struct NativeString *out);
void MediaItem_GetCategory(struct MediaItem *item, struct NativeString *out);
void MediaItem_GetDescription(struct MediaItem *item, struct NativeString *out);
void MediaItem_GetShowName(struct MediaItem *item, struct NativeString *out);
void MediaItem_GetDateAdded(struct DateTime *out, struct MediaItem *item);
void MediaItem_GetDateModified(struct DateTime *out, struct MediaItem *item);
void MediaItem_GetLastPlayed(struct DateTime *out, struct MediaItem *item);
void MediaItem_GetLastSkipped(struct DateTime *out, struct MediaItem *item);
int MediaItem_GetReleaseDate(struct MediaItem *item, struct DateTime *out);
int MediaListView_IsValid(void *list_view);
uint16_t MediaListView_GetCount(void *list_view);
int MediaListView_RemoveEntryAtIndex(void *list_view, uint32_t index, int order_mode,
                                     uint32_t reverse);
int MediaEntry_Remove(struct MediaEntry *entry);
void MediaList_NotifyChanged(struct MediaList *list);
struct MediaEntry *PlaybackList_GetEntryByIndex(void *list, uint32_t index);
void MediaList_InvalidateOrderCache(struct MediaList *list, uint32_t order_mode);
void *MediaList_CopyEntryAtPosition(void *source, void *destination, void *cursor,
                                    uint32_t flags);
const uint64_t *MediaList_GetOrderSortFields(struct MediaList *list,
                                             uint32_t order_mode);
int SongRecord_CompareField(void *collation, void *left, void *right,
                            uint64_t field_mask);
struct MediaEntry *MediaList_GetEntryByOrderIndex(struct MediaList *list,
                                                  int order_mode, uint32_t reverse,
                                                  uint32_t index);
int MediaList_GetEntryOrderIndex(void *entry, uint32_t order_mode, uint32_t reverse);
int MediaList_BuildOrderIndex(struct MediaList *list, uint32_t order);
void MediaList_ResolveOrderMode(struct MediaList *list, uint32_t *order,
                                uint8_t *reverse);
int MediaListController_AddSelectionToOnTheGo(void *controller);
int MediaListController_DispatchAction(void *controller, const char *action,
                                       uint32_t argument);
void MediaListController_HandleShowContextualMenu(void *controller);
int MediaListController_MapVisibleRow(void *controller, int row);
int NestedPlaylists_MapVisibleRow(void *controller, int row);
int OnTheGo_AddBrowserSelection(void *otg, int row, void *browser, int save);
uint32_t MediaListSelection_SelectArtworkSource(void *selection, uint32_t category);
void *MediaListSelection_GetInstance(void);
void *MediaListSelection_SetTitle(void *selection, const struct NativeString *title);
void ArtworkSession_Stop(void *session);
void *ArtworkSession_Construct(void *session, const char *name);
void *ArtworkSession_Destroy(void *session);
int ArtworkSession_Attach(void *session, void *listener);
int ArtworkSession_SetFormat(void *session, int format);
int ArtworkSession_BindQuery(void *session);
int ArtworkSession_Center(void *session, int absolute, int value);
int ArtworkWindow_Center(void *window, int absolute, int value);
int ArtworkSession_Start(void *session);
int PlaybackArtwork_GetImage(void *session, void *out_image, int index,
                             uint8_t *out_available, int format);
void *AlbumArtworkProvider_Create(uint32_t source_kind, uint32_t selector);
void *Genius_GetInstance(void);
int Genius_MapVisibleRow(void *genius, int row);
void Genius_GetItem(void *genius, uint32_t row, void *item);
int ITunesDB_ImportTrackDataset(void *reader);
int ITunesDB_ImportCachedString(void *reader, uint32_t encoding, uint32_t mhod_type,
                                void *cache, uint32_t *token, uint32_t glyph_field,
                                int collect);
int ITunesDB_SkipImportBytes(void *reader, int bytes);
void TrackData_AcquireStringCaches(void *track_data);
void TrackData_ReleaseStringCaches(void *track_data);
void TrackData_DestroyStorage(void *track_data);
void *MediaBrowser_GetOwner(void *browser);
struct MediaList *MediaBrowser_GetCurrentList(void *browser);
int MediaBrowser_CreateRoot(void *system, void **out_browser);
int MediaBrowser_CreateChild(void *owner, void **out);
int MediaBrowser_EvaluateEntryFilters(void *browser, struct MediaEntry *entry,
                                      struct MediaList *list_override);
void MediaBrowser_CopyMatchingEntries(void *browser, struct MediaList *source,
                                      uint32_t order_mode, uint32_t reverse,
                                      struct MediaList *destination,
                                      int apply_skip_flags, uint32_t selected_index,
                                      struct MediaEntry **selected_out);
int MediaBrowser_InvalidateEntryMatches(void *browser);
void MediaBrowser_ClearArtists(void *browser);
void MediaBrowser_ClearAlbums(void *browser);
void MediaBrowser_ClearSongs(void *browser);
uint32_t MediaBrowser_GetRecordArtworkId(void *owner, void *record);
int MediaBrowser_AccumulateArtistGroup(void *browser, void *callback,
                                       uint32_t field_low, uint32_t field_high,
                                       uint32_t index, struct MediaEntry *entry,
                                       void *record, int first);
int MediaBrowser_AccumulateAlbumGroup(void *browser, void *callback, uint32_t field_low,
                                      uint32_t field_high, uint32_t index,
                                      struct MediaEntry *entry, void *record,
                                      int first);
uint32_t MediaBrowser_BuildArtists(void *browser);
int MediaBrowser_BuildSongs(void *browser);
int MediaBrowser_ReadArtistName(void *browser, int index, uint16_t *counted_out);
int MediaBrowser_ReadArtistJumpLabel(void *browser, int index, uint16_t *counted_out);
int MediaBrowser_ReadAlbumName(void *browser, int index, uint16_t *counted_out);
int MediaBrowser_SelectAlbumIndex(void *browser, int index);
uint32_t MediaBrowser_GetOrderMode(void *browser);
void AlbumGroupVector_Append(void *vector, const void *row);
void *LibraryBrowser_Create(uint32_t ownership);
void *LibraryBrowser_InitBorrowed(void *model, uint8_t item_flags, void *browser);
void *LibraryBrowser_Destroy(void *model);
void LibraryBrowser_CopyAlbumArtistSubtitle(void *model, int index,
                                            struct NativeString *out);
void *LibraryModelRef_Init(void *reference, void *model, int synchronized);
void *LibraryModelRef_InitCopy(void *reference, const void *source);
void *LibraryModelRef_Assign(void *destination, void *source);
void LibraryModelRef_Release(void *reference);
void *LibraryModelRef_Get(void *reference);
uint32_t MediaPlayer_GetReadAheadBytes(void *player);
int FixedPool_Create(uint32_t slot_bytes, uint32_t argument, void **out_pool);
uint32_t Heap_GetTotalBytes(void);
void *BlockCache_Get(void);
uint32_t BlockCache_GetBlockBytes(void);
uint32_t BlockCache_GetTotalBlocks(void);
int BlockCache_Grow(uint32_t bytes);
void BlockCache_Trim(void);
int BlockCache_PostRebalance(void *cache, uint32_t argument);
int BlockCache_PostWaitPending(void *cache, uint32_t argument);
int BlockCache_PostClientRequest(void *cache, uint32_t argument);
uint32_t BlockCache_AddBlocks(void *cache, uint32_t count);
int BlockCache_PushFreeBlock(void *cache, void *block);
uint32_t BlockCache_GetAssignedBlockCount(void *cache);
uint32_t BlockCache_CountPendingClients(void *cache);
void BlockCache_IncrementPendingClientCount(void *cache);
void BlockList_Erase(void **out_next, void *list, void **node);
int BlockCacheClient_SetDesiredBytes(void *client, uint32_t bytes, uint32_t weight);
void BlockCacheClient_ReturnBlock(void *client, void *block);
int BlockCacheClient_CancelPending(void *client);
int EAppBlockManager_HandleMessage(void *manager, void *message);
void EAppBlocks_Acquire(void *manager);
void EAppBlocks_Release(void *manager);
int MediaStream_Seek(void *stream, uint32_t position_ms);
int MP4Parser_SeekToTime(void *parser, uint32_t position_ms);
int MP4Parser_SeekAudioTrack(void *parser, uint32_t position_ms);
int MP4Parser_SeekPrimaryTrack(void *parser, uint32_t *position_ms, uint32_t flags);
int MP4Parser_LoadSampleSizeWindow(void *parser, uint32_t first_index);
int MP4Parser_LoadSampleToChunkWindow(void *parser, uint32_t first_index);
int MP4Parser_LoadChunkOffsetWindow(void *parser, uint32_t first_index);
int MP4Parser_ReadBigEndianWords(void *parser, uint32_t *buffer, uint32_t offset_low,
                                 uint32_t offset_high, uint32_t count);
void MP4Parser_LoadTrackEdit(void *parser, void *track);
void MP4Parser_LoadPrimaryTrackEdit(void *parser);
uint64_t Runtime_UDiv64(uint64_t numerator, uint64_t divisor);
void PMU_SetBacklightEnabled(int enabled);
int PMU_ReadMemory(uint32_t offset, uint32_t count, void *buffer);
int PMU_WriteMemory(uint32_t offset, uint32_t count, const void *buffer);
uint32_t Power_EnterLightSleep(int mode);
void *PowerController_Get(void);
uint32_t Battery_GetChargeFraction(void);
uint32_t BatteryGauge_GetLevel(void *gauge, int allow_drop);
void PowerController_PostEvent(void *controller, uint32_t event);
void PowerController_NotifyGameStarted(void *controller);
void PowerController_HandleGameStarted(void *controller);
void PowerController_HandleIdleTimeout(void *controller);
void PowerController_SetSleepAfterWake(void *controller);
void TaskMessage_PostBegin(uint32_t client, uint32_t flag);
void TaskMessage_PostEndAfter(uint32_t client, int milliseconds);
int Disk_GetPowerState(uint32_t *out_powered);
void *StorageArbiter_Get(void);
void StorageArbiter_Activate(void *arbiter, void *owner);
void StorageArbiter_SetOwner(void *arbiter, void *client);
void StorageArbiter_Request(void *arbiter, void *client);
void StorageArbiter_Release(void *arbiter, void *client);
int DiskClient_Yield(void *client);
void *ScsiDiskClient_Create(void *memory, void *scsi_context);
void ScsiDiskClient_Request(void *client);
void HardwareEvent_Post(uint32_t source);
void HardwareEvent_PostWithArgument(uint32_t source, uint32_t argument);
int HardwareEvent_MapSource(uint32_t source, uint8_t *out_index);
int HardwareEvent_Dispatch(uint32_t index, uint32_t *argument);
void App_HandleDiskModeRequest(void *app);
void App_PollDiskModeEntry(void *app);
void App_EnterDiskMode(void *app);
void App_ExitDiskMode(void *app);
void System_PrepareDiskMode(void *system);
void App_HandleDiskGranted(void *app);
void App_HandleReloadTimer(void *app);
void App_StartMusicLoading(void *app);
void App_PollMusicLoading(void *app);
void *MusicLoadingTask_Create(void *memory);
int MusicLoadingTask_Run(void *task);
void ResourceBank_InitOverrides(void *bank);
void ResourceBank_SetResource(void *bank, uint32_t type, uint32_t id, const void *data,
                              uint32_t bytes);
void *ResourceBank_GetResource(void *bank, uint32_t type, uint32_t id,
                               uint32_t *out_bytes);
const void *ResourceBank_FirstResource(void *bank, uint32_t type, uint32_t *id,
                                       uint32_t *bytes);
const void *ResourceBank_NextResource(void *bank, uint32_t *id, uint32_t *bytes);
void ResourceBank_RemoveHeapResource(void *bank, uint32_t type, uint32_t id);
void *ResourceBank_Destroy(void *bank);
int ResourceOverrides_Find(void *map, uint32_t type, uint32_t id, uint32_t **entry);
int ResourceOverrides_EraseEntry(void *map, uint32_t type, const uint32_t *entry);
uint32_t *ResourceNames_FindOrInsertSlot(void *map, const void *name);
void ResourceBlockList_AppendBorrowedBlock(void *list, const void *data, uint32_t bytes,
                                           uint32_t metadata);
void ResourceDtTm_CopyDefault(void *out_date);
char *ResourceService_GetString(void *service, uint32_t id);
void *ResourceService_GetBitmap(void *service, uint32_t id);
void *UI_GetResourceBank(void);
void UI_SetResourceBank(void *bank);
void *UI_GetResourceService(void);
uint32_t ResourceName_GetId(void *name);
void *Heap_AllocateZeroed(uint32_t byte_count);
void Heap_Free(void *allocation);
void *Runtime_OperatorNew(uint32_t byte_count);
int Mutex_Lock(void *mutex);
int Mutex_Unlock(void *mutex);
void *Mutex_Init(void *mutex);
int HoldSwitch_GetLogicalState(void);
int HoldSwitch_ReadHardwareBit(uint32_t *out_raw_bit);
int Platform_InitMicrosecondCounter(void);
void SystemUI_HandleHoldChange(void *system, int locked);
int File_OpenLegacyPath(void *path, uint32_t fork, int mode, void **out_handle);
int File_ReadLegacyHandle(void *handle, void *buffer, uint32_t bytes, uint32_t flags,
                          uint32_t *out_read);
int File_WriteLegacyHandle(void *handle, const void *buffer, uint32_t bytes,
                           uint32_t flags, uint32_t *out_written);
int File_CloseLegacyHandle(void *handle);
int File_SeekLegacyHandle(void *handle, uint64_t offset);
int File_SeekLegacyHandle64(void *handle, int64_t offset, int whence);
int File_GetLegacyHandleSize(void *handle, uint32_t *out_bytes);
int BufferedFile_GetPosition(void *file, uint64_t *position);
void Preferences_LoadFile(void *preferences);
void Preferences_FlushIfDirty(void *preferences);
void Preferences_MarkDirty(void *preferences);
void Preferences_SetClickerRoute(void *browser, uint32_t route);
uint32_t Preferences_GetClickerRoute(void *browser);
void *PreferencesService_Get(void);
int File_DeleteLegacyPath(void *path);
void Runtime_Abort(void);
void Runtime_ReportUncaughtException(int kind, const char *what);
struct DirectoryIterator *DirectoryIterator_Init(struct DirectoryIterator *iterator,
                                                 const char *path, uint8_t recursive,
                                                 uint32_t volume);
int DirectoryIterator_Next(struct DirectoryIterator *iterator, struct NativePath *path,
                           uint8_t *is_directory);
void DirectoryIterator_Destroy(struct DirectoryIterator *iterator);
int Directory_CreateWithParents(const char *path, uint32_t volume);
struct NativePath *NativePath_InitEmpty(struct NativePath *path);
struct NativePath *NativePath_Destroy(struct NativePath *path);
struct NativePath *NativePath_InitFromString(struct NativePath *path,
                                             const struct NativeString *string);
int File_RenameNativePaths(struct NativePath *source, struct NativePath *destination,
                           uint32_t volume);
void *Thread_GetCurrentRecord(void);
void Scheduler_YieldOrDelay(uint32_t delay);
void *IRQ_Dispatch(uint32_t *frame);
uint32_t Clock_GetMicroseconds(void);
int RTC_ReadDateTime(struct DateTime *out);
void RTC_GetZoneOffsets(int16_t *zone_minutes, uint8_t *dst_minutes);
int TimeZone_GetOffsetMinutes(uint32_t zone_id);
int TimeZone_IsDSTActive(uint32_t zone_id);
char *SetWarmBootModeTag(uint32_t mode);
void ResetSocAndWait(void);
void Timer_Stop(void *timer);
void *FileHandle_Open(void *handle, const char *path, int read_only, int volume,
                      uint32_t buffer_bytes, int flag, int argument);
void *FileHandle_OpenString(void *handle, struct NativeString *path, int read_only,
                            int volume, uint32_t buffer_bytes, int flag, int argument);
int FileHandle_OpenNative(void *handle);
int FileHandle_IsOpen(void *handle);
int FileHandle_Read(void *handle, uint32_t byte_count, void *buffer,
                    uint32_t *out_count, uint32_t flags);
int FileHandle_Write(void *handle, uint32_t byte_count, const void *buffer,
                     uint32_t *out_count, uint32_t flags);
void File_StoreLegacyHandleSize(void *handle, uint32_t *out_bytes);
uint32_t Preferences_GetLanguage(void);
void App_StoreBrightness(void *app, int value);
int Settings_HandleAction(void *controller, const char *action, uint32_t argument);
void *SettingsModel_GetInstance(void);
void SettingsModel_ToggleClicker(void *model);
void SettingsModel_NotifyClicker(void *model);
int SettingsModel_GetProperty(void *model, uint32_t type, uint32_t property,
                              uintptr_t *out);
int SettingsModel_GetIndexedProperty(void *model, uint32_t type, uint32_t property,
                                     uint32_t index, uintptr_t *out);
void SettingsModel_ToggleMainMenuItem(void *model, uint32_t index);
void SettingsModel_ToggleMusicMenuItem(void *model, uint32_t index);
void *SettingsModel_ApplyMainMenuPreferences(void *model);
void *SettingsModel_ApplyMusicMenuPreferences(void *model);
void *SettingsModel_ResetMainMenu(void *model);
void *SettingsModel_ResetMusicMenu(void *model);
void *SettingsModel_GetEQPresetPreview(void *model, uint32_t menu_index);
void *SettingsModel_GetEQPresetName(void *model, uint32_t menu_index);
void SettingsModel_ResetAllSettings(void *model);
int Settings_SetEQPreset(void *browser, uint32_t menu_index);
void SettingsModel_SelectEQMenuIndex(void *model, uint32_t menu_index);
void SettingsModel_SetPreviewIndex(void *model, uint32_t index);
int SettingsEQ_HandleAction(void *controller, const char *action, uint32_t argument);
int SettingsEQ_ApplySelection(void *controller);
int SettingsEQ_ShowPreview(void *controller);
int SettingsMainMenu_HandleAction(void *controller, const char *action,
                                  uint32_t argument);
int SettingsEQ_HandleEvent(void *controller, void *event);
int SettingsEQ_SelectCurrentRow(void *controller, uint32_t deferred);
int SettingsBacklight_SelectCurrentRow(void *controller, uint32_t deferred);
int SettingsMusicMenu_HandleAction(void *controller, const char *action,
                                   uint32_t argument);
void SettingsModel_AdjustBrightness(void *model, int delta);
void *CppString_Init(void *string, const char *text);
void *CppString_InitBytes(void *string, const void *data, uint32_t bytes,
                          void *allocator);
void CppString_Destroy(void *string);
int String_CompareCString(const char *left, const char *right);
void StringInputStream_Init(void *stream, void *string, uint32_t mode);
void StringInputStream_Destroy(void *stream, uint32_t flags, void *storage);
struct NativeString *NativeString_InitEmpty(struct NativeString *string);
struct NativeString *NativeString_Destroy(struct NativeString *string);
char *NativeString_GetUtf8(struct NativeString *string);
struct NativeString *NativeString_InitUtf8(struct NativeString *string,
                                           const char *utf8);
void NativeString_AssignUtf8(struct NativeString *string, const char *text);
void NativeString_AssignUtf16(struct NativeString *string, const uint16_t *text,
                              int characters);
int SongText_CompareUtf16(void *collation, const uint16_t *left, uint32_t left_units,
                          const uint16_t *right, uint32_t right_units);
int StringCache_ReadToken(void *cache, uint32_t token, void *destination,
                          uint32_t *bytes, uint32_t maximum_bytes);
int StringCache_ReleaseToken(void *cache, uint32_t token);
int StringCache_GetTokenView(void *cache, uint32_t token, void **data, uint32_t *bytes);
int PodcastEpisodes_DispatchAction(void *controller, const char *action,
                                   uint32_t argument);
int PodcastEpisodes_BuildAndShowContextualMenu(void *controller);
int ITunesUEpisodes_DispatchAction(void *controller, const char *action,
                                   uint32_t argument);
int ITunesUEpisodes_BuildAndShowContextualMenu(void *controller);
int Audiobooks_BuildAndShowContextualMenu(void *controller);
int Movies_DispatchAction(void *controller, const char *action, uint32_t argument);
int TVEpisodes_DispatchAction(void *controller, const char *action, uint32_t argument);
int NestedVideoPlaylists_DispatchAction(void *controller, const char *action,
                                        uint32_t argument);
int MediaListController_ShowContextMenuDefault(void *controller, int row);
void MenuList_RebuildVisibleItems(void *menu_list, void *view,
                                  int refresh_subscriptions);
int MenuList_SetItemTable(void *menu_list, uint32_t table_id, void *view);
void MenuItems_RegisterResourceType(uint32_t type);
void UI_ShowContextMenuItems(void *controller, void *items);
void *ContextMenuModel_Get(void);
void ContextMenuModel_SetItems(void *model, const void *items);
uint32_t ContextMenuModel_GetRememberedIndex(void *model);
void UI_SetScreenState(void *manager, uint32_t screen_id, uint32_t state0,
                       uint32_t state1, uint32_t state2, uint32_t state3);
int ContextMenuModel_GetProperty(void *model, uint32_t type, uint32_t property,
                                 uintptr_t *out);
int ContextMenuModel_GetIndexedProperty(void *model, uint32_t type, uint32_t property,
                                        int index, uintptr_t *out);
void UI_LoadLayoutInstances(void *layout);
int ContextMenuController_DispatchAction(void *controller, const char *action,
                                         uint32_t argument);
void ScreenController_SwitchLayoutDefault(void *controller);
void *UI_GetLayoutManager(void);
int MenuList_QueryProperty(void *menu_list, uint32_t type, uint32_t property,
                           uint32_t *out);
void *ControllerRegistry_InvokeControllerFactory(void *registry, const char *class_name,
                                                 const char *controller_name);
void *ControllerRegistry_InvokeScreenFactory(void *registry, const char *class_name,
                                             const char *controller_name,
                                             uint32_t screen_id, uint32_t layout_id,
                                             uint32_t transition);
void *UI_FindServiceById(uint32_t service_id);
void *UI_GetRootView(void);
void *UI_GetSelectionModel(void);
void *UI_GetControllerRegistry(void);
void *UI_CreateScreenController(void *registry, uint32_t screen_id, uint32_t layout_id,
                                uint32_t transition);
void UI_RegisterControllerFactories(void *registry, const char *name,
                                    void *ordinary_factory, void *resource_factory);
void UI_RegisterViewFactory(uint32_t view_type, void *create);
void UI_LoadLayoutEventTables(void *layout);
void UIEventMap_ReadStream(void *stream, void *map);
void *ScreenController_CreateNamed(const char *name, uint32_t screen_id,
                                   uint32_t layout_id, uint32_t transition);
int ScreenController_HandleEvent(void *controller, void *event);
uint32_t ResourceScreen_Activate(void *controller, uint32_t entering);
uint32_t ResourceScreen_Deactivate(void *controller, uint32_t leaving);
int MenuItem_SetVisible(uint32_t item_id, uint32_t visible);
void Navigator_PopToScreen(uint32_t screen_id, uint32_t push_history, uint32_t skip_history);
void *Source_LoadDescriptors(uint32_t resource_id);
void *TextMenuRow_Initialize(void *row, int submenu);
void ContextMenuItems_Append(void *items, uint32_t item_id, const char *action,
                             uint32_t flags);
void *ContextMenuItems_Init(void *items, uint8_t flag);
void *ContextMenuItems_Destroy(void *items);
void WheelEvent_DecodeMotion(void *event);
uint32_t ButtonEvent_GetUIKeyCode(void *event);
void *WheelAccumulator_Init(void *state, int threshold, uint8_t accelerate,
                            uint32_t interval);
int WheelAccumulator_ConsumeMotion(void *state, int motion, uint32_t speed);
void WheelAccumulator_Reset(void *accumulator);
int WheelAccumulator_AccumulateMotion(void *state, int motion, uint32_t speed);
void WheelAccumulator_SetThreshold(void *state, int threshold);
void WheelAccumulator_SetMaxMultiplier(void *state, int max_multiplier);
int WheelAccumulator_IsSlowTurn(void *state, uint32_t speed);
int WheelAccumulator_IsFastTurn(void *state, uint32_t speed);
void View_InitOptionsFromDescriptor(const void *descriptor, void *options);
void View_ResolveBounds(void *view, int32_t *bounds);
void *View_Construct(void *view, void *unused, void *parent, void *event_target,
                     const void *options);
void View_BindGraphicsContext(void *view, void *context);
void View_GetLocalContentRect(void *view, int32_t *rect);
void View_GetLocalBounds(void *view, int32_t *rect);
void View_GetWindowBounds(void *view, int32_t *rect);
void View_BindGraphicsContextToBounds(void *view, void *context);
void View_InvalidateRect(void *view, const int32_t *rect);
void *View_GetWindow(void *view);
void Window_InvalidateRect(void *window, const int32_t *rect);
void View_DrawBackgroundAndBorder(void *view);
void View_Draw(void *view, void *draw_argument, const int32_t *clip);
void View_DrawClipped(void *view, void *draw_argument, const int32_t *clip);
int View_HandleModelNotification(void *view, void *event);
void UI_RegisterBuiltinViewFactories(void);
void ProgressBar_RefreshFromModel(void *view);
void ProgressBar_DrawMarkers(void *view, void *context, const int32_t *bar);
int ProgressBar_FillWidthForValue(void *view, int value, const int32_t *bar);
int ProgressBar_ComputeFillWidth(void *view, int minimum, int maximum, int value,
                                 const int32_t *bar);
void ProgressBar_LoadBitmapCaches(void *view);
void ProgressBar_Draw(void *view, void *draw_argument, const int32_t *clip);
void ProgressBarStyle8_Draw(void *view, void *draw_argument, const int32_t *clip);
void *TextView_CreateFromDescriptor(void *owner, void *parent, void *event_target,
                                    const void *descriptor);
int MainMediaMenu_DispatchAction(void *controller, const char *action,
                                 uint32_t argument);
int MainMediaMenu_DispatchCommonAction(void *controller, const char *action,
                                       uint32_t argument);
uint32_t MusicMenu_ConfigureLibraryBrowser(void *controller, uint32_t include_kinds,
                                           uint32_t exclude_kinds, int reset,
                                           uint32_t grouping, uint32_t sort,
                                           uint32_t option);
int NowPlaying_DispatchAction(void *controller, const char *action, uint32_t argument);
int SongList_DispatchAction(void *controller, const char *action, uint32_t argument);
int NestedPlaylists_DispatchAction(void *controller, const char *action,
                                   uint32_t argument);
int Genius_DispatchAction(void *controller, const char *action, uint32_t argument);
void UI_InitializeEventTimer(void *controller, void *timer, uint32_t period,
                             void *event_queue);
void Timer_Start(void *timer);
void UITask_Main(void);
void Timer_SetPeriod(void *timer, uint32_t milliseconds);
int Controller_ExecuteNavigatorAction(void *controller, void *action,
                                      int action_context, uint8_t *stop_dispatch);
void Navigator_PopTop(uint32_t flags);
void Navigator_PopToMainScreen(void);
void Navigator_PopToPixoMainScreen(void);
int ExtrasMenu_DispatchAction(void *controller, const char *action, uint32_t argument);
void *UI_ConstructScreenDefinition(void *definition, uint32_t screen_id,
                                   int load_layouts);
void UI_LoadScreenLayouts(void *definition);
void *ScreenDefinition_Destruct(void *definition);
void *Layout_Destruct(void *layout);
uint32_t SettingsMenuListController_Deactivate(void *controller, uint32_t leaving);
uint32_t GamesMenuController_Deactivate(void *controller, uint32_t leaving);
int ResourceScreen_HandleEvent(void *controller, void *event);
int ScreenController_DispatchNamedEvent(void *controller, void *event);
int BrightnessController_HandleEvent(void *controller, void *event);
int BrightnessController_HandleWheel(void *controller, void *event);
int WheelInput_QuantizeMotion(uint32_t motion, int threshold);
uint32_t WheelEvent_GetSpeed(void *event);
int ScreenController_PopSelf(void *controller, uint32_t flags);
void *ResourceService_GetIndexedResource(void *service, uint32_t type,
                                         uint32_t property, uint32_t index);
int MenuList_GetRowProperty(void *list_model, uint32_t type, uint32_t property,
                            int index, void *out);
void MenuItems_RegisterAll(void);
void BitmapView_LoadBitmap(void *view);
void *Object_GetInterface(void *object, uint32_t interface);
uint8_t Layout_GetKind(void *layout);
int UI_InstanceEnterStyle(void *manager, void *instance, int transition_5,
                          int other_kind);
int UI_InstanceLeaveStyle(void *manager, void *instance, int transition_5,
                          int other_kind);
void *UI_GetCompositorLayer(void *layers, uint32_t index);
int UI_EmitControllerEvent(void *controller, void *event_name);
void *ControllerLayoutCache_GetOrInsert(void *definition, uint32_t layout_id);
void *UI_CreateTemplateFromResource(void *context, void *owner, void *event_target,
                                    const void *options, uint32_t view_id,
                                    uint32_t argument);
void *UI_ConstructLayout(void *layout, uint32_t layout_id, uint8_t kind,
                         const void *screen_list_record);
void **UIntPointerMap_GetOrInsert(void *map, const uint32_t *key);
void *View_FindViewById(void *view, uint32_t id);
void *UI_FindTemplateView(void *owner, uint32_t template_id);
void UI_DisplayPreparedLayout(void *manager, void *layout);
void *Navigator_Get(void);
int NavigatorProperties_Has(void *properties, void *key);
int NavigatorProperties_GetInteger(void *properties, void *key);
void NavigatorProperties_SetInteger(void *properties, void *key, const uint32_t *value);
void *NavigatorProperties_SetString(void *properties, void *key, void *value);
int NavigatorProperties_Remove(void *properties, void *key);
void Navigator_Push(void *navigator, void *controller, uint32_t flags);
void *UI_GetRootOwner(void);
void TextView_DrawString(void *view, const char *text, void *argument);
void *UI_ResolveTemplateView(void *manager, uint32_t template_id,
                             uint8_t *out_from_root);
void BitmapView_Draw(void *view, void *draw_argument, const int32_t *clip);
void MenuRowBitmapView_Draw(void *view, void *draw_argument, const int32_t *clip);
void KeyboardView_Draw(void *view, void *draw_argument, const int32_t *clip);
void KeyboardView_DrawKeyRow(void *view, void *context, const int32_t *rect);
void KeyboardView_DrawKey(void *view, void *context, int key, const int32_t *rect,
                          int selected);
void OptionBarView_Draw(void *view, void *draw_argument, const int32_t *clip);
void OptionBarView_SetSelected(void *bar, uint32_t index);
void CalendarView_DrawClipped(void *view, void *draw_argument, const int32_t *clip);
void CalendarDayCell_Draw(void *view, void *draw_argument, const int32_t *clip);
void GLAlbumArtView_Draw(void *view, void *draw_argument, const int32_t *clip);
void *GLAlbumArtView_GetImage(void *view);
void GLAlbumArtAnimation_RenderCover(void *animation);
int UI_ProcessMessage(int wait, uint8_t *out_quit);
void UI_RunMessageLoop(void);
void Model_Notify(void *model, uint32_t type, uint32_t property);
void *SelectionGradient_GetBitmap(void);
void *MediaArtworkProvider_Get(void);
void *MediaArtworkProvider_GetSlotArtwork(void *provider, uint32_t slot);
void *AlarmsService_Get(void);
void AlarmsService_SelectSleepTimer(void *service, uint32_t choice);
void USB_HandleSetupPacket(void *usb);
void *USB_DispatchControlRead(void *usb, uint32_t *inout_length);
void *USB_HandleVendorDeviceRead(void *usb, uint32_t *inout_length);
int USB_DispatchNoDataRequest(void *usb);
int USB_HandleAppleVendorCommand(void *usb, uint32_t setup0, uint32_t setup1);
void *USB_GetControlWriteBuffer(void *usb, uint32_t length);
void USB_StartControlTransfer(void *usb, uint32_t endpoint, void *buffer,
                              uint32_t length);
int USB_IsAccessoryMode(void);
void SCSI_AcquireMedium(void *out_result, void *context, int set_sense);

extern void *const ThreeBandEQ_vtable[];
extern uint8_t AudioOutputQueue[];
extern const uint8_t EQ_Presets[];
extern const uint32_t EQ_PresetIds[];
extern const uint8_t DisplayLayerOrders[];
extern const uint32_t DisplayBlendRegisters[];
extern uint32_t FairPlay_StorageHandle;
extern const uint8_t GameAPI_ModuleRegistry[];
extern const char GameImport_EndOfListName[];
extern const uint8_t GameSettings_Table[];
extern const uint8_t GameSettings_TimeFormatTable[];
extern const uint8_t GameSettings_LanguageTable[];
extern uint8_t GameBacklight[];
extern void *const GameList_vtable[];
extern const uint8_t EGL_ProcedureTable[];
extern const uint8_t GameAPI_MiscTBD_Module[];
extern const uint8_t GameAPI_DebugUtil_Module[];
extern const uint8_t GameAPI_MemoryAlloc_Module[];
extern const uint8_t GameAPI_InputEvents_Module[];
extern const uint8_t GameAPI_Settings_Module[];
extern const uint8_t GameAPI_AsyncFileIO_Module[];
extern const uint8_t GameAPI_SoundEffect_Module[];
extern const uint8_t GameAPI_OpenGLES_Module[];
extern const uint8_t GameAPI_Audio_Module[];
extern const uint8_t GameAPI_Metadata_Module[];
extern const uint8_t GameAPI_Users_Module[];
extern void *const Bitmap_vtable[];
extern uint8_t Palette_White[4];
extern uint8_t Palette_LightGray[4];
extern uint8_t Palette_Gray[4];
extern uint8_t Palette_DarkGray[4];
extern uint8_t Palette_Black[4];
extern uint8_t EventManager[];
extern volatile uint32_t WheelController[];
extern uint8_t WheelState[];
extern const uint8_t ButtonKeyCodes[5];
extern const uint8_t UIKeyNames_Button[];
extern const uint8_t UIKeyNames_Remote[];
extern const uint8_t UIKeyNames_Video[];
extern const uint8_t UIKeyNames_Image[];
extern const uint8_t UIKeyNames_Audio[];
extern const uint8_t UIKeyNames_Mikey[];
extern const char *const InputEventNameWords[14];
extern void *NowPlayingModel_Instance;
extern void *const MediaPlayer_vtable[];
extern uint8_t BrowserRegistry[];
extern void *BlockCache;
extern void *MainHeap;
extern uint16_t ATA_Identify[256];
extern void *const MediaStream_vtable[];
extern void *const MP4Parser_vtable[];
extern volatile uint32_t I2C0Controller[];
extern volatile uint32_t ClockRegister_PowerGate1;
extern uint8_t StorageArbiter[];
extern uint8_t DiskClient[];
extern void *const DiskClient_vtable[];
extern void *const ScsiDiskClient_vtable[];
extern void *const MusicLoadingTask_vtable[];
extern void *App;
extern void *PowerController_Instance;
extern const uint32_t PowerController_Transitions[];
extern const uint32_t PowerController_StateHandlers[];
extern uint8_t ResourceNameMap[];
extern const uint8_t ResourceRomImage[];
extern uint32_t ClickerRoute;
extern const uint8_t TimeZones[];
extern const uint8_t TimeZoneOffsets[];
extern uint8_t IRQ_Nesting;
extern uint32_t MainMenuSettingsRows[];
extern uint32_t MusicMenuSettingsRows[];
extern void *const ScreenController_vtable[];
extern void *const View_vtable[];
extern uint32_t SleepTimerChoices[];
extern void *const TemplateView_vtable[];
extern void *const ProgressBar_vtable[];
extern void *const ProgressBarStyle8_vtable[];
extern void *const SportTimerChosenDispatcher_vtable[];
extern void *const SettingsMenuListController_vtable[];
extern void *const ExtrasMenuController_vtable[];
extern void *const GamesMenuController_vtable[];
extern void *const ArtistsController_vtable[];
extern void *const AlbumsController_vtable[];
extern void *const NestedPlaylistsController_vtable[];
extern void *const NestedVideoPlaylistsController_vtable[];
extern void *const ITunesUEpisodesController_vtable[];
extern void *const AudiobooksController_vtable[];
extern void *const MoviesController_vtable[];
extern void *const TVEpisodesController_vtable[];
extern uint8_t Navigator[];
extern uint8_t NavigatorProperties[];
extern void *const TextView_vtable[];
extern void *const SelectedRowTextView_vtable[];
extern void *const MenuList_vtable[];
extern void *const TextMenuRow_vtable[];
extern void *const MenuListModel_vtable[];
extern void *const GLAlbumArtView_vtable[];
extern uint8_t SelectionBar_RamGradient[2][4];
extern uint8_t SelectionBar_RamHighlight[4];
extern uint8_t SelectionGradient_Bitmap[];
extern void *MediaArtworkProvider;
#endif

#define ADDR_ArtworkDatabase_Import 0x08059000
#define ADDR_ArtworkDatabase_ReadImageThumbs 0x0808b578
#define ADDR_ArtworkDatabase_FindById 0x08045534
#define ADDR_ArtworkDatabase_FindByDbid 0x08045668
#define ADDR_ArtworkThumb_Destroy 0x0803e16c
#define ADDR_ArtworkThumb_GetNameCache 0x080475cc
#define ADDR_EQ_LoadPresetCoefficients 0x082734c0
#define ADDR_EQ_ResetAllBiquadStates 0x08273630
#define ADDR_EQ_ResetBiquadState 0x08273594
#define ADDR_EQ_ProcessBiquadStereoS16 0x08273658
#define ADDR_EQ_ProcessThreeBiquadsInPlace 0x082737fc
#define ADDR_EQ_SetDspPreset 0x082735c4
#define ADDR_EQ_SetSampleRate 0x082735f4
#define ADDR_EQ_InitThreeBiquadState 0x08273858
#define ADDR_EQ_SettingsIdToDspPreset 0x080b28a0
#define ADDR_EQ_SettingsIdToMenuIndex 0x080bb460
#define ADDR_EQ_MenuIndexToSettingsId 0x0807bf94
#define ADDR_MediaTrack_ApplyEQ 0x08173b48
#define ADDR_MediaTrack_IsReady 0x0829394c
#define ADDR_Audio_SetPlaybackSource 0x081a64c4
#define ADDR_Audio_InitEffectsChain 0x0818ced8
#define ADDR_Audio_ProcessEffectsAndOutput 0x0818cbe0
#define ADDR_Audio_SetPostEffectsGainQ15 0x0818cec0
#define ADDR_Audio_SetProperty 0x0815cff4
#define ADDR_AudioService_SetProperty 0x220050ac
#define ADDR_Audio_SetSpeed 0x0818cb4c
#define ADDR_Audio_SetSourceFormat 0x0818cdf0
#define ADDR_Audio_SetInputFrames 0x0818ca7c
#define ADDR_Audio_SetOutputFormat 0x0818ca88
#define ADDR_Audio_GetOutputFrames 0x0818cb68
#define ADDR_Audio_SetEQPreset 0x0818ca70
#define ADDR_AudioMixer_Get 0x22007490
#define ADDR_Audio_FillOutputBuffer 0x22007840
#define ADDR_AudioMixer_MixOutputBuffer 0x22007d3c
#define ADDR_AudioMixer_ScaleStereoQ15 0x22007514
#define ADDR_AudioMixer_SetOutputLevel 0x220077cc
#define ADDR_Audio_SetOutputLevel 0x081a66c0
#define ADDR_AudioMixer_SetSoundCheckGain 0x22007c78
#define ADDR_AudioOutputDevice_Get 0x0818d000
#define ADDR_AudioOutputDevice_SetSoundCheckGain 0x081ea6f0
#define ADDR_Audio_RepositionDecoder 0x220054c8
#define ADDR_AudioOutputQueue_Rebuild 0x220072ec
#define ADDR_AudioOutputQueue_Read 0x22006b84
#define ADDR_AudioOutputQueue_Get 0x22006ea8
#define ADDR_AudioOutputQueue_Write 0x22006f60
#define ADDR_AudioOutputQueue_Get_thunk 0x0802d100
#define ADDR_AudioOutputQueue_Write_thunk 0x0802d220
#define ADDR_Platform_UsesVolumeWarningPolicy 0x08047450
#define ADDR_Volume_GetRegionalCeiling 0x081558c0
#define ADDR_Volume_GetDefaultLimit 0x08155ad0
#define ADDR_VolumePolicy_IsWarningAcknowledged 0x08155a88
#define ADDR_MediaPlayer_FinishCurrentTrack 0x081731d8
#define ADDR_MediaTrack_ApplyPlaybackSettings 0x081737b8
#define ADDR_MediaTrack_ApplyVolumeAdjustment 0x08173994
#define ADDR_Volume_Get 0x081557d0
#define ADDR_Volume_Apply 0x08155d00
#define ADDR_MediaPlayer_PostVolumeExposureLimitEvent 0x080cfe74
#define ADDR_DisplayLayer_Commit 0x08114be4
#define ADDR_DisplayPipe_ConfigureWindow 0x08143c30
#define ADDR_DisplayPipe_SetWindowSource 0x081445cc
#define ADDR_DisplayPipe_SetWindowEnable 0x081440b8
#define ADDR_DisplayPipe_SetWindowBlendMode 0x0814472c
#define ADDR_DisplayPipe_SetWindowAlpha 0x08144410
#define ADDR_DisplayPipe_SetOrder 0x0814482c
#define ADDR_DisplayPipe_SetGammaTables 0x08088abc
#define ADDR_DisplayPipe_Init 0x08144158
#define ADDR_FairPlay_VerifyGameFileAndCreateContext 0x082cdef4
#define ADDR_FairPlay_DecryptBuffer 0x080632a4
#define ADDR_FairPlay_ReleaseContextAdapter 0x0804bbbc
#define ADDR_GameManifest_InitReader 0x0825e510
#define ADDR_GameManifest_ReadWithOptionalSignature 0x0825e428
#define ADDR_GameLoader_LoadVerifiedManifest 0x080f9374
#define ADDR_GameLoader_ReadVerifyAndDecryptExecutable 0x080f971c
#define ADDR_GameLoader_ResolveImportsAndEntryPoints 0x080e91f8
#define ADDR_GameMemory_Allocate 0x082c0e38
#define ADDR_GameMemory_Reallocate 0x082c0e64
#define ADDR_GameMemory_Free 0x082bdcb8
#define ADDR_GameInput_ReadWheel 0x082bda94
#define ADDR_GameSettings_GetValue 0x082bdf20
#define ADDR_GameRuntime_RequestLaunch 0x0828c3e4
#define ADDR_GameRuntime_LoadManifestAndLaunch 0x0828d2a0
#define ADDR_GameRuntime_InvokeFrameCallback 0x0828c72c
#define ADDR_GameRuntime_ShouldContinueFrames 0x0828d610
#define ADDR_GameRuntime_AppendInputEvent 0x0828c644
#define ADDR_GameFiles_BuildPath 0x08096f58
#define ADDR_GameFiles_GetStorageDirectoryName 0x0808cfc8
#define ADDR_GameCatalog_ReadManifestMetadata 0x0806f564
#define ADDR_GameDirectory_BuildList 0x081e46a0
#define ADDR_GamesService_Get 0x080f5d54
#define ADDR_GamesService_BuildList 0x080f543c
#define ADDR_GamesService_ScanRoot 0x080f5b48
#define ADDR_GamesService_ClearList 0x080f5858
#define ADDR_GamesService_Init 0x080f62b0
#define ADDR_GamesService_HandleEvent 0x080f6a18
#define ADDR_GameListEntry_Construct 0x08120ea0
#define ADDR_GameListEntry_Destroy 0x08120fa8
#define ADDR_GameList_RemoveEntry 0x08392ab8
#define ADDR_GameList_RemoveAt 0x08392a5c
#define ADDR_GameList_Destroy 0x08392af8
#define ADDR_GamesService_GetIndexedProperty 0x080f63b8
#define ADDR_GamesService_RequestSelectedGameLaunch 0x080f6708
#define ADDR_GamesService_GetArtworkId 0x080f5af0
#define ADDR_GamesService_GetArtworkPath 0x080f6264
#define ADDR_GamesService_GetArtworkStorage 0x080f55cc
#define ADDR_GamesService_StopGame 0x080f591c
#define ADDR_GamesService_AbortGame 0x080f55a4
#define ADDR_GamesService_ResetAfterGame 0x080f6918
#define ADDR_GameSystem_SetBacklightEnabled 0x082c0f90
#define ADDR_GameSystem_SetBacklightLevel 0x082c0fa8
#define ADDR_GameSystem_GetBacklightLevel 0x082bdd18
#define ADDR_GameSystem_SetBacklightDimmed 0x082bd9bc
#define ADDR_GameSystem_Sleep 0x082bd9b8
#define ADDR_GameSystem_GetDateTime 0x082bddbc
#define ADDR_GameSystem_GetBatteryLevel 0x082bdd28
#define ADDR_GameSystem_IsExternalPowerConnected 0x082c0e24
#define ADDR_GameSystem_GetPath 0x082bde10
#define ADDR_GameSystem_GetGameName 0x082bdd4c
#define ADDR_GameSystem_GetGamesPlatformId 0x082bdce0
#define ADDR_GameSystem_IsModelCode1 0x082bdf04
#define ADDR_Clock_ReadMicroseconds 0x0807bffc
#define ADDR_GameDebug_OpenFile 0x082bdaf8
#define ADDR_GameDebug_CloseFile 0x082bdaa4
#define ADDR_GameDebug_ReadFile 0x082bdc20
#define ADDR_GameDebug_WriteFile 0x082bdc6c
#define ADDR_GameDebug_Printf 0x082c0e60
#define ADDR_GameInput_ForwardButtonEvent 0x082c0e98
#define ADDR_GameFile_QueueOpen 0x082bd3ec
#define ADDR_GameFile_QueueClose 0x082bd3d0
#define ADDR_GameFile_QueueTransfer 0x082bd44c
#define ADDR_GameFile_QueueReadFile 0x082bd420
#define ADDR_GameFile_QueueWriteFile 0x082bd468
#define ADDR_GameFile_QueueDelete 0x082c1134
#define ADDR_GameFile_QueueRename 0x082c1160
#define ADDR_GameDirectory_QueueList 0x082bda44
#define ADDR_GameDirectory_Rewind 0x082bda78
#define ADDR_GameDirectory_ReadEntry 0x082bda10
#define ADDR_GameDirectory_ReleaseList 0x082bd9d4
#define ADDR_GameDirectory_GetStatus 0x082bd9f4
#define ADDR_GameSaveFile_Open 0x082bd918
#define ADDR_GameSaveFile_Retain 0x082bd940
#define ADDR_GameSaveFile_Close 0x082bd900
#define ADDR_GameSaveFile_Read 0x082bd958
#define ADDR_GameSaveFile_Write 0x082bd990
#define ADDR_GameSound_Create 0x082c03b8
#define ADDR_GameSound_Destroy 0x082c0470
#define ADDR_GameSound_Play 0x082c08b4
#define ADDR_GameSound_Pause 0x082c0870
#define ADDR_GameSound_Resume 0x082c0900
#define ADDR_GameSound_Stop 0x082c0d9c
#define ADDR_GameSound_Unused 0x082c0de0
#define ADDR_GameSound_SetData 0x082c09c4
#define ADDR_GameSound_SetByteCount 0x082c0a08
#define ADDR_GameSound_SetFlagC 0x082c0984
#define ADDR_GameSound_SetSampleRate 0x082c0d1c
#define ADDR_GameSound_SetChannels 0x082c0bdc
#define ADDR_GameSound_SetBitsPerSample 0x082c0944
#define ADDR_GameSound_SetVolume 0x082c0d5c
#define ADDR_GameSound_SetPan 0x082c0c1c
#define ADDR_GameSound_SetSpeed 0x082c0c5c
#define ADDR_GameSound_SetPlayCount 0x082c0a88
#define ADDR_GameSound_SetState 0x082c0ac8
#define ADDR_GameSound_SetFlags 0x082c0a48
#define ADDR_GameSound_SetTailMilliseconds 0x082c0c9c
#define ADDR_GameSound_SetLeadInMilliseconds 0x082c0cdc
#define ADDR_GameSound_SetNext 0x082c0b88
#define ADDR_GameSound_SetMusicDuck 0x082c0b40
#define ADDR_GameSound_GetData 0x082c0578
#define ADDR_GameSound_GetByteCount 0x082c05ac
#define ADDR_GameSound_GetFlagC 0x082c0544
#define ADDR_GameSound_GetSampleRate 0x082c07c4
#define ADDR_GameSound_GetChannels 0x082c06c0
#define ADDR_GameSound_GetBitsPerSample 0x082c04d4
#define ADDR_GameSound_GetVolume 0x082c07f8
#define ADDR_GameSound_GetSpeed 0x082c0728
#define ADDR_GameSound_GetPlayCount 0x082c061c
#define ADDR_GameSound_GetPan 0x082c06f4
#define ADDR_GameSound_GetSpeedScale 0x082c04cc
#define ADDR_GameSound_GetZero 0x082c04c4
#define ADDR_GameSound_GetState 0x082c0650
#define ADDR_GameSound_GetFlags 0x082c05e0
#define ADDR_GameSound_GetTailMilliseconds 0x082c075c
#define ADDR_GameSound_GetLeadInMilliseconds 0x082c0790
#define ADDR_GameSound_GetVoicePosition 0x082c0508
#define ADDR_GameSound_GetNext 0x082c068c
#define ADDR_GameSound_IsPlaying 0x082c082c
#define ADDR_glActiveTexture 0x082c767c
#define ADDR_glAlphaFunc 0x082c769c
#define ADDR_glAlphaFuncx 0x082c76c4
#define ADDR_glBindBuffer 0x082c76ec
#define ADDR_glBindTexture 0x082c7714
#define ADDR_glBlendFunc 0x082c773c
#define ADDR_glBufferData 0x082c7764
#define ADDR_glBufferSubData 0x082c7798
#define ADDR_glClear 0x082c77cc
#define ADDR_glClearColor 0x082c77ec
#define ADDR_glClearColorx 0x082c7820
#define ADDR_glClearDepthf 0x082c7854
#define ADDR_glClearDepthx 0x082c7874
#define ADDR_glClearStencil 0x082c7894
#define ADDR_glClientActiveTexture 0x082c78b4
#define ADDR_glClipPlanex 0x082c78fc
#define ADDR_glClipPlanef 0x082c78d4
#define ADDR_glColor4f 0x082c7924
#define ADDR_glColor4x 0x082c7958
#define ADDR_glColorMask 0x082c798c
#define ADDR_glColorPointer 0x082c79c0
#define ADDR_glCompressedTexImage2D 0x082c79f4
#define ADDR_glCompressedTexSubImage2D 0x082c7a40
#define ADDR_glCopyTexImage2D 0x082c7a88
#define ADDR_glCopyTexSubImage2D 0x082c7ad4
#define ADDR_glCullFace 0x082c7b20
#define ADDR_glDeleteBuffers 0x082c7b40
#define ADDR_glDeleteTextures 0x082c7b68
#define ADDR_glDepthFunc 0x082c7b90
#define ADDR_glDepthMask 0x082c7bb0
#define ADDR_glDepthRangef 0x082c7bd0
#define ADDR_glDepthRangex 0x082c7bf8
#define ADDR_glDisable 0x082c7c20
#define ADDR_glDisableClientState 0x082c7c40
#define ADDR_glDrawArrays 0x082c7c60
#define ADDR_glDrawElements 0x082c7c90
#define ADDR_glEnable 0x082c7cc4
#define ADDR_glEnableClientState 0x082c7ce4
#define ADDR_glFinish 0x082c7d04
#define ADDR_glFlush 0x082c7d1c
#define ADDR_glFogf 0x082c7d34
#define ADDR_glFogfv 0x082c7d5c
#define ADDR_glFogx 0x082c7d84
#define ADDR_glFogxv 0x082c7dac
#define ADDR_glFrontFace 0x082c7dd4
#define ADDR_glFrustumf 0x082c7df4
#define ADDR_glFrustumx 0x082c7e30
#define ADDR_glGetBooleanv 0x082c7ebc
#define ADDR_glGetBufferParameteriv 0x082c7ee4
#define ADDR_glGetClipPlanef 0x082c7f34
#define ADDR_glGetClipPlanex 0x082c7f5c
#define ADDR_glGenBuffers 0x082c7e6c
#define ADDR_glGenTextures 0x082c7e94
#define ADDR_glGetError 0x082c7f84
#define ADDR_glGetFixedv 0x082c7f9c
#define ADDR_glGetFloatv 0x082c7fc4
#define ADDR_glGetIntegerv 0x082c7fec
#define ADDR_glGetLightfv 0x082c8014
#define ADDR_glGetLightxv 0x082c8044
#define ADDR_glGetMaterialfv 0x082c8074
#define ADDR_glGetMaterialxv 0x082c80a4
#define ADDR_glGetPointerv 0x082c80d4
#define ADDR_glGetString 0x082c80fc
#define ADDR_glGetTexEnviv 0x082c814c
#define ADDR_glGetTexEnvfv 0x082c811c
#define ADDR_glGetTexEnvxv 0x082c817c
#define ADDR_glGetTexParameteriv 0x082c81dc
#define ADDR_glGetTexParameterfv 0x082c81ac
#define ADDR_glGetTexParameterxv 0x082c820c
#define ADDR_glHint 0x082c823c
#define ADDR_glIsBuffer 0x082c8264
#define ADDR_glIsEnabled 0x082c8284
#define ADDR_glIsTexture 0x082c82a4
#define ADDR_glLightModelf 0x082c82c4
#define ADDR_glLightModelfv 0x082c82ec
#define ADDR_glLightModelx 0x082c8314
#define ADDR_glLightModelxv 0x082c833c
#define ADDR_glLightf 0x082c8364
#define ADDR_glLightfv 0x082c8394
#define ADDR_glLightx 0x082c83c4
#define ADDR_glLightxv 0x082c83f4
#define ADDR_glLineWidth 0x082c8424
#define ADDR_glLineWidthx 0x082c8444
#define ADDR_glLoadIdentity 0x082c8464
#define ADDR_glLoadMatrixf 0x082c847c
#define ADDR_glLoadMatrixx 0x082c849c
#define ADDR_glLogicOp 0x082c84bc
#define ADDR_glMaterialf 0x082c84dc
#define ADDR_glMaterialfv 0x082c850c
#define ADDR_glMaterialx 0x082c853c
#define ADDR_glMaterialxv 0x082c856c
#define ADDR_glMatrixMode 0x082c859c
#define ADDR_glMultMatrixf 0x082c85bc
#define ADDR_glMultMatrixx 0x082c85dc
#define ADDR_glMultiTexCoord4f 0x082c85fc
#define ADDR_glMultiTexCoord4x 0x082c8638
#define ADDR_glNormal3f 0x082c8674
#define ADDR_glNormal3x 0x082c86a4
#define ADDR_glNormalPointer 0x082c86d4
#define ADDR_glOrthof 0x082c8704
#define ADDR_glOrthox 0x082c8740
#define ADDR_glPixelStorei 0x082c877c
#define ADDR_glPointParameterf 0x082c87a4
#define ADDR_glPointParameterfv 0x082c87cc
#define ADDR_glPointParameterx 0x082c87f4
#define ADDR_glPointParameterxv 0x082c881c
#define ADDR_glPointSize 0x082c8844
#define ADDR_glPointSizex 0x082c8894
#define ADDR_glPolygonOffset 0x082c88b4
#define ADDR_glPolygonOffsetx 0x082c88dc
#define ADDR_glPopMatrix 0x082c8904
#define ADDR_glPushMatrix 0x082c891c
#define ADDR_glReadPixels 0x082c895c
#define ADDR_glRotatef 0x082c89c8
#define ADDR_glRotatex 0x082c89fc
#define ADDR_glSampleCoverage 0x082c8a30
#define ADDR_glSampleCoveragex 0x082c8a58
#define ADDR_glScalef 0x082c8a80
#define ADDR_glScalex 0x082c8ab0
#define ADDR_glScissor 0x082c8ae0
#define ADDR_glShadeModel 0x082c8b3c
#define ADDR_glStencilFunc 0x082c8b5c
#define ADDR_glStencilMask 0x082c8b8c
#define ADDR_glStencilOp 0x082c8bac
#define ADDR_glTexCoordPointer 0x082c8bdc
#define ADDR_glTexEnvf 0x082c8c10
#define ADDR_glTexEnvfv 0x082c8c40
#define ADDR_glTexEnvx 0x082c8c70
#define ADDR_glTexEnvxv 0x082c8ca0
#define ADDR_glTexImage2D 0x082c8cd0
#define ADDR_glTexParameteri 0x082c8d48
#define ADDR_glTexParameterf 0x082c8d18
#define ADDR_glTexParameterx 0x082c8d78
#define ADDR_glTexSubImage2D 0x082c8da8
#define ADDR_glTranslatef 0x082c8df0
#define ADDR_glTranslatex 0x082c8e20
#define ADDR_glVertexPointer 0x082c8e70
#define ADDR_glViewport 0x082c8ea4
#define ADDR_glQueryMatrixxOES 0x082c8934
#define ADDR_eglGetError 0x082c1a3c
#define ADDR_eglGetDisplay 0x082c1a38
#define ADDR_eglInitialize 0x082c1a4c
#define ADDR_eglTerminate 0x082c1ba8
#define ADDR_eglQueryString 0x082c1ad0
#define ADDR_eglGetConfigs 0x082c19e0
#define ADDR_eglChooseConfig 0x082c17b0
#define ADDR_eglGetConfigAttrib 0x082c19c0
#define ADDR_eglCreateWindowSurface 0x082c18f4
#define ADDR_eglCreatePixmapSurface 0x082c1850
#define ADDR_eglCreatePbufferSurface 0x082c1808
#define ADDR_eglDestroySurface 0x082c19ac
#define ADDR_eglQuerySurface 0x082c1b4c
#define ADDR_eglSurfaceAttrib 0x082c1b78
#define ADDR_eglBindTexImage 0x082c17a8
#define ADDR_eglReleaseTexImage 0x082c1b6c
#define ADDR_eglSwapInterval 0x082c1ba0
#define ADDR_eglCreateContext 0x082c17e8
#define ADDR_eglDestroyContext 0x082c1998
#define ADDR_eglMakeCurrent 0x082c1a6c
#define ADDR_eglGetCurrentContext 0x082c19f0
#define ADDR_eglGetCurrentSurface 0x082c19fc
#define ADDR_eglGetCurrentDisplay 0x082c19f4
#define ADDR_eglQueryContext 0x082c1aac
#define ADDR_eglWaitGL 0x082c1bb0
#define ADDR_eglWaitNative 0x082c1bb8
#define ADDR_eglSwapBuffers 0x082c1b80
#define ADDR_eglCopyBuffers 0x082c17c4
#define ADDR_EGL_StoreSurfaceWord 0x082c1b74
#define ADDR_glRegisterBinaryShaderAPPLE 0x082c89a0
#define ADDR_glUnregisterBinaryShaderAPPLE 0x082c8e50
#define ADDR_GameAudio_GetRepeatMode 0x082bd5b4
#define ADDR_GameAudio_SetRepeatMode 0x082bd828
#define ADDR_GameAudio_GetPlaybackOrder 0x082bd5c4
#define ADDR_GameAudio_SetPlaybackOrder 0x082bd840
#define ADDR_GameAudio_RefreshCurrentEntry 0x082bd708
#define ADDR_GameAudio_SetShuffleOrderBuilt 0x082bd758
#define ADDR_GameAudio_SetEQMenuIndexTemporary 0x082bd7ac
#define ADDR_GameAudio_SetEQMenuIndex 0x082bd7d4
#define ADDR_GameAudio_GetEQMenuIndex 0x082bd574
#define ADDR_GameAudio_SetSpeed 0x082bd810
#define ADDR_GameAudio_GetSpeed 0x082bd594
#define ADDR_GameAudio_SetGain 0x082bd7f8
#define ADDR_GameAudio_GetGain 0x082bd584
#define ADDR_GameAudio_InitPlaybackList 0x082bd6a0
#define ADDR_GameAudio_AppendMediaSelection 0x082bd4a4
#define ADDR_GameAudio_RemoveEntryAtIndex 0x082bd6d0
#define ADDR_GameAudio_ClearPlaybackList 0x082bd4d4
#define ADDR_GameAudio_SetPlaybackChanged 0x082bd4e4
#define ADDR_GameAudio_PlayIndex 0x082bd644
#define ADDR_GameAudio_Stop 0x082bd888
#define ADDR_GameAudio_TogglePlayPause 0x082bd8c8
#define ADDR_GameAudio_StartScanForward 0x082bd868
#define ADDR_GameAudio_StartScanBackward 0x082bd878
#define ADDR_GameAudio_StopScan 0x082bd898
#define ADDR_GameAudio_Seek 0x082bd728
#define ADDR_GameAudio_SetItemStartPosition 0x082bd78c
#define ADDR_GameAudio_NextTrack 0x082bd634
#define ADDR_GameAudio_PreviousTrack 0x082bd6c0
#define ADDR_GameAudio_GetCurrentIndex 0x082bd4fc
#define ADDR_GameAudio_GetEntryCount 0x082bd8e0
#define ADDR_GameAudio_GetItemHandleAtIndex 0x082bd51c
#define ADDR_GameAudio_GetPosition 0x082bd8a8
#define ADDR_GameAudio_GetRemainingPosition 0x082bd8b8
#define ADDR_GameAudio_GetPlaybackState 0x082bd5a4
#define ADDR_GameAudio_IsPlaying 0x082bd614
#define ADDR_GameAudio_IsPaused 0x082bd604
#define ADDR_GameAudio_IsStopped 0x082bd624
#define ADDR_GameAudio_IsScanningForward 0x082bd5d4
#define ADDR_GameAudio_IsScanningBackward 0x082bd5f4
#define ADDR_GameAudio_IsScanning 0x082bd5e4
#define ADDR_GameAudio_IsShuffleEnabled 0x082bd858
#define ADDR_GameAudio_IsRepeatOne 0x082bd6f8
#define ADDR_GameAudio_IsRepeatAll 0x082bd6e8
#define ADDR_GameAudio_IsPlaybackOrderReversed 0x082bd718
#define ADDR_GameAudio_IsPlayingOnTheGo 0x082bd66c
#define ADDR_GameAudio_IsPlaybackListUnrestricted 0x082bd494
#define ADDR_GameAudio_HasPlaybackList 0x082bd8f0
#define ADDR_GameAudio_GetVolume 0x082bd50c
#define ADDR_GameAudio_GetVolumeLimit 0x082bd534
#define ADDR_GameAudio_SetVolume 0x082bd740
#define ADDR_GameAudio_GetMixerLevel 0x082bd564
#define ADDR_GameAudio_SetMixerLevel 0x082bd770
#define ADDR_GameAudio_GetVersion 0x082bd544
#define ADDR_GameAudio_RequestPowerHold 0x082bd67c
#define ADDR_GameMetadata_PhotoLibraryCreate 0x082bf108
#define ADDR_GameMetadata_PhotoLibraryDestroy 0x082beeb0
#define ADDR_GameMetadata_PhotoLibraryGetAlbumCount 0x082bee94
#define ADDR_GameMetadata_PhotoLibraryGetImageCount 0x082bf140
#define ADDR_GameMetadata_PhotoLibraryGetFormatCount 0x082beecc
#define ADDR_GameMetadata_PhotoLibraryFindAlbum 0x082bf050
#define ADDR_GameMetadata_PhotoLibraryGetAlbumAt 0x082bef50
#define ADDR_GameMetadata_PhotoLibraryGetImageAt 0x082befe4
#define ADDR_GameMetadata_PhotoLibraryGetImageById 0x082bf09c
#define ADDR_GameMetadata_PhotoLibraryGetFormatIdAt 0x082befc0
#define ADDR_GameMetadata_PhotoLibraryGetFormat 0x082beef0
#define ADDR_GameMetadata_PhotoAlbumDestroy 0x082be258
#define ADDR_GameMetadata_PhotoAlbumIsValid 0x082bee78
#define ADDR_GameMetadata_PhotoAlbumCopyName 0x082bee14
#define ADDR_GameMetadata_PhotoAlbumGetImageCount 0x082bee5c
#define ADDR_GameMetadata_PhotoAlbumGetImageAt 0x082beca8
#define ADDR_GameMetadata_PhotoAlbumGetByte27 0x082bedf8
#define ADDR_GameMetadata_PhotoAlbumGetPlaylist 0x082bed18
#define ADDR_GameMetadata_PhotoAlbumGetTrack 0x082bed88
#define ADDR_GameMetadata_ArtworkLibraryCreate 0x082be0d4
#define ADDR_GameMetadata_ArtworkLibraryDestroy 0x082be008
#define ADDR_GameMetadata_ArtworkLibraryGetFormatCount 0x082be024
#define ADDR_GameMetadata_ArtworkLibraryGetFormatIdAt 0x082be0a8
#define ADDR_GameMetadata_ArtworkLibraryGetFormat 0x082be048
#define ADDR_GameMetadata_ArtworkLibraryGetTrackImage 0x082be79c
#define ADDR_GameMetadata_ImageDestroy 0x082be204
#define ADDR_GameMetadata_ImageIsValid 0x082bebf4
#define ADDR_GameMetadata_ImageGetId 0x082beb98
#define ADDR_GameMetadata_ImageGetDate 0x082beb48
#define ADDR_GameMetadata_ImageIsSame 0x082bebbc
#define ADDR_GameMetadata_ImageCountThumbs 0x082be834
#define ADDR_GameMetadata_ImageGetThumbAt 0x082be86c
#define ADDR_GameMetadata_ImageGetThumbBeforeAbove 0x082be954
#define ADDR_GameMetadata_ImageDataDestroy 0x082be220
#define ADDR_GameMetadata_ImageDataLoad 0x082beb04
#define ADDR_GameMetadata_ImageDataGetDimensions 0x082bea5c
#define ADDR_GameMetadata_ImageDataReadDimensions 0x082be930
#define ADDR_GameMetadata_ImageDataGetLoadedHeader 0x082be914
#define ADDR_GameMetadata_ImageDataGetFormat 0x082be9fc
#define ADDR_GameMetadata_ImageDataGetPixels 0x082beb24
#define ADDR_GameMetadata_ImageDataGetByteCount 0x082beae0
#define ADDR_GameMetadata_ImageDataGetFileExtent 0x082bea80
#define ADDR_GameMetadata_FilterCreate 0x082be170
#define ADDR_GameMetadata_FilterDestroy 0x082be23c
#define ADDR_GameMetadata_FilterCopyName 0x082be19c
#define ADDR_GameMetadata_LibraryCreate 0x082bec74
#define ADDR_GameMetadata_LibraryDestroy 0x082bec5c
#define ADDR_GameMetadata_LibraryReset 0x082be12c
#define ADDR_GameMetadata_LibrarySelectPlaylist 0x082bf4b0
#define ADDR_GameMetadata_LibrarySelectArtist 0x082bf3cc
#define ADDR_GameMetadata_LibrarySelectAlbum 0x082bf390
#define ADDR_GameMetadata_LibrarySelectGenre 0x082bf444
#define ADDR_GameMetadata_LibrarySelectComposer 0x082bf408
#define ADDR_GameMetadata_LibrarySelectTVShow 0x082bf514
#define ADDR_GameMetadata_LibrarySelectSeasonAt 0x082bf4ec
#define ADDR_GameMetadata_LibrarySetBrowserFlagEF9 0x082be814
#define ADDR_GameMetadata_LibrarySetBrowserFlagEF8 0x082be1e4
#define ADDR_GameMetadata_LibrarySetKindMasks 0x082bf480
#define ADDR_GameMetadata_LibraryGetAlbumCount 0x082bdfe4
#define ADDR_GameMetadata_LibraryGetArtistCount 0x082be108
#define ADDR_GameMetadata_LibraryGetGenreCount 0x082be2ac
#define ADDR_GameMetadata_LibraryGetPlaylistCount 0x082bf15c
#define ADDR_GameMetadata_LibraryGetComposerCount 0x082be14c
#define ADDR_GameMetadata_LibraryGetTrackCount 0x082bf86c
#define ADDR_GameMetadata_LibraryGetTVShowCount 0x082bf550
#define ADDR_GameMetadata_LibraryGetSeasonCount 0x082bf36c
#define ADDR_GameMetadata_LibraryGetFilteredPlaylistCount 0x082c0394
#define ADDR_GameMetadata_LibrarySetOrderMode1 0x082bf594
#define ADDR_GameMetadata_LibrarySetOrderMode3 0x082bf574
#define ADDR_GameMetadata_LibrarySetOrderMode0 0x082bf5f4
#define ADDR_GameMetadata_LibrarySetOrderMode9 0x082bf5d4
#define ADDR_GameMetadata_LibrarySetOrderMode2 0x082bf5b4
#define ADDR_GameMetadata_LibrarySetOrderMode4 0x082bf614
#define ADDR_GameMetadata_LibrarySetOrderMode5 0x082bf634
#define ADDR_GameMetadata_LibrarySetOrderMode8 0x082bf654
#define ADDR_GameMetadata_LibrarySetOrderMode7 0x082bf674
#define ADDR_GameMetadata_LibrarySetOrderFlag 0x082bf344
#define ADDR_GameMetadata_LibraryGetOrderFlag 0x082bec38
#define ADDR_GameMetadata_LibraryGetTrackAt 0x082be538
#define ADDR_GameMetadata_LibraryFindTrack 0x082be714
#define ADDR_GameMetadata_LibraryGetArtistAt 0x082be320
#define ADDR_GameMetadata_LibraryGetAlbumAt 0x082be2d0
#define ADDR_GameMetadata_LibraryGetGenreAt 0x082be438
#define ADDR_GameMetadata_LibraryGetComposerAt 0x082be3e8
#define ADDR_GameMetadata_LibraryGetTVShowAt 0x082be4e8
#define ADDR_GameMetadata_LibraryGetSeasonNumberAt 0x082be4bc
#define ADDR_GameMetadata_LibraryGetAlbumImage 0x082be370
#define ADDR_GameMetadata_LibraryClearSelections 0x082bec18
#define ADDR_GameMetadata_LibraryGetFilteredPlaylistAt 0x082be5b8
#define ADDR_GameMetadata_LibraryGetPlaylistAt 0x082be488
#define ADDR_GameMetadata_LibraryFindPlaylist 0x082be668
#define ADDR_GameMetadata_LibraryGetConstant 0x082be644
#define ADDR_GameMetadata_LibraryCopyTableString 0x082be5ec
#define ADDR_GameMetadata_LibraryGetTrackDataByte905 0x082c0090
#define ADDR_GameMetadata_LibraryGetTrackDataByte906 0x082c026c
#define ADDR_GameMetadata_TrackDestroy 0x082be290
#define ADDR_GameMetadata_TrackIsValid 0x082c0328
#define ADDR_GameMetadata_TrackGetPersistentId 0x082bfbb8
#define ADDR_GameMetadata_TrackGetEntryId 0x082bffc4
#define ADDR_GameMetadata_TrackGetEntryWord18 0x082bfff0
#define ADDR_GameMetadata_TrackCopyPath 0x082c001c
#define ADDR_GameMetadata_TrackCopyTitle 0x082bff50
#define ADDR_GameMetadata_TrackCopyAlbum 0x082bf694
#define ADDR_GameMetadata_TrackCopyArtist 0x082bf6e4
#define ADDR_GameMetadata_TrackCopyGenre 0x082bfaac
#define ADDR_GameMetadata_TrackCopyComposer 0x082bf81c
#define ADDR_GameMetadata_TrackCopyGrouping 0x082bfafc
#define ADDR_GameMetadata_TrackCopyFileType 0x082bfdd0
#define ADDR_GameMetadata_TrackCopyComment 0x082bf7cc
#define ADDR_GameMetadata_TrackCopyCategory 0x082bf77c
#define ADDR_GameMetadata_TrackCopyDescription 0x082bf8e8
#define ADDR_GameMetadata_TrackCopyShowName 0x082c0184
#define ADDR_GameMetadata_TrackGetSeasonNumber 0x082c0160
#define ADDR_GameMetadata_TrackCopyPodcastRssUrl 0x082c00b4
#define ADDR_GameMetadata_TrackCopyDisplayArtist 0x082c0290
#define ADDR_GameMetadata_TrackCopyRecordString48 0x082bf9cc
#define ADDR_GameMetadata_TrackGetBookmarkMs 0x082bf758
#define ADDR_GameMetadata_TrackGetStartTimeMs 0x082c02e0
#define ADDR_GameMetadata_TrackGetStopTimeMs 0x082c0304
#define ADDR_GameMetadata_TrackGetDurationSeconds 0x082bf95c
#define ADDR_GameMetadata_TrackGetDurationMs 0x082bf980
#define ADDR_GameMetadata_TrackGetFileSizeBytes 0x082c0224
#define ADDR_GameMetadata_TrackGetBitrateKbps 0x082bf734
#define ADDR_GameMetadata_TrackGetYear 0x082c0370
#define ADDR_GameMetadata_TrackGetRating 0x082c0104
#define ADDR_GameMetadata_TrackGetTrackNumber 0x082bffa0
#define ADDR_GameMetadata_TrackGetDiscNumber 0x082bf938
#define ADDR_GameMetadata_TrackGetPlayCount 0x082c006c
#define ADDR_GameMetadata_TrackGetSkipCount 0x082c0248
#define ADDR_GameMetadata_TrackGetEQSettingsId 0x082bfa1c
#define ADDR_GameMetadata_TrackGetVolumeAdjustment 0x082c034c
#define ADDR_GameMetadata_TrackHasRecordFlag1C40 0x082bfb4c
#define ADDR_GameMetadata_TrackRemembersPosition 0x082bfb70
#define ADDR_GameMetadata_TrackHasRecordFlag8F10 0x082bfb94
#define ADDR_GameMetadata_TrackIsAudiobook 0x082bfc44
#define ADDR_GameMetadata_TrackIsMP4Audio 0x082bfcf8
#define ADDR_GameMetadata_TrackIsM4A 0x082bfc68
#define ADDR_GameMetadata_TrackIsM4B 0x082bfc8c
#define ADDR_GameMetadata_TrackIsM4P 0x082bfcb0
#define ADDR_GameMetadata_TrackIsMP3 0x082bfcd4
#define ADDR_GameMetadata_TrackIsWAV 0x082bfdac
#define ADDR_GameMetadata_TrackIsAIFF 0x082bfc20
#define ADDR_GameMetadata_TrackHasRecordFlag8F01 0x082bfd40
#define ADDR_GameMetadata_TrackIsVideo 0x082bfd88
#define ADDR_GameMetadata_TrackIsTVShow 0x082bfd64
#define ADDR_GameMetadata_TrackIsMusicVideo 0x082bfd1c
#define ADDR_GameMetadata_TrackGetReleaseDate 0x082c0128
#define ADDR_GameMetadata_TrackSetBookmarkMs 0x082c01d4
#define ADDR_GameMetadata_TrackSetRating 0x082c01fc
#define ADDR_GameMetadata_TrackIncrementPlayCount 0x082bfbe0
#define ADDR_GameMetadata_TrackIncrementSkipCount 0x082bfc00
#define ADDR_GameMetadata_TrackGetLastPlayed 0x082bfe48
#define ADDR_GameMetadata_TrackGetDateAdded 0x082bf890
#define ADDR_GameMetadata_TrackGetDateModified 0x082bfef8
#define ADDR_GameMetadata_TrackGetLastSkipped 0x082bfea0
#define ADDR_GameMetadata_TrackGetRecordWordF4 0x082bfa88
#define ADDR_GameMetadata_TrackGetPregap 0x082bfa40
#define ADDR_GameMetadata_TrackGetPostgap 0x082bfa64
#define ADDR_GameMetadata_TrackGetSampleCount 0x082bf9a4
#define ADDR_GameMetadata_TrackGetGaplessData 0x082bfe20
#define ADDR_GameMetadata_PlaylistDestroy 0x082be274
#define ADDR_GameMetadata_PlaylistIsValid 0x082bf320
#define ADDR_GameMetadata_PlaylistGetPersistentId 0x082bf200
#define ADDR_GameMetadata_PlaylistGetCount 0x082bf2fc
#define ADDR_GameMetadata_PlaylistCopyName 0x082bf24c
#define ADDR_GameMetadata_PlaylistIsLibrary 0x082bf228
#define ADDR_GameMetadata_PlaylistGetTrackAt 0x082bf180
#define ADDR_GameMetadata_PlaylistRemoveEntryAt 0x082bf2c0
#define ADDR_GameMetadata_PlaylistRefresh 0x082bf29c
#define ADDR_GameUser_GetCount 0x082c1090
#define ADDR_GameUser_GetAt 0x082c1058
#define ADDR_GameUser_FindById 0x082c1004
#define ADDR_GameUser_Remove 0x082c0fe8
#define ADDR_GameUser_Add 0x082c0fc0
#define ADDR_GameUser_GetName 0x082c1118
#define ADDR_GameUser_CopyDateA 0x082c10a8
#define ADDR_GameUser_CopyDateB 0x082c10d8
#define ADDR_GameUser_GetId 0x082c1108
#define ADDR_GameUser_SetName 0x082c1128
#define ADDR_eglSaveSurfaceHM 0x0808fa08
#define ADDR_glPointSizePointerOES 0x082c8864
#define ADDR_GameSettings_ReadMappedString 0x08073f5c
#define ADDR_GameSettings_ReadLanguageIndex 0x0807c04c
#define ADDR_GameBacklight_Get 0x081b1894
#define ADDR_GameBacklight_Teardown 0x081b1998
#define ADDR_GameBacklight_SetLevel 0x081b1928
#define ADDR_GameBacklight_SetDimmed 0x081b18ec
#define ADDR_GameSaveTable_Close 0x0813eee0
#define ADDR_GameUserList_Add 0x0812bb00
#define ADDR_GameUserList_Transfer 0x0812bb78
#define ADDR_GameMetadata_GetTrackers 0x080fb1dc
#define ADDR_GameMetadata_DestroyTrackers 0x080fb2bc
#define ADDR_GameAudio_GetCachedSpeed 0x0817f7ec
#define ADDR_GameAudio_QueuePreviousTrack 0x0817f528
#define ADDR_GameAudio_ReadPlayingOnTheGo 0x0817eb70
#define ADDR_GameAudio_ReadPlaybackListUnrestricted 0x0817f054
#define ADDR_GameAudio_ReadMixerLevel 0x0817f09c
#define ADDR_GameAudio_SnapshotPlayerSettings 0x0817f43c
#define ADDR_GameAudio_RestorePlayerSettings 0x0817f6f8
#define ADDR_GameAudio_ApplyInitPlaybackList 0x0817f380
#define ADDR_GameAudio_ApplyMediaSelection 0x0817ef2c
#define ADDR_GameAudio_ApplyRemoveEntryAtIndex 0x0817f190
#define ADDR_GameAudio_ApplyClearPlaybackList 0x0817f14c
#define ADDR_GameAudio_ApplyPlaybackChanged 0x0817f3dc
#define ADDR_AudioMixer_GetMusicLevel 0x081a6b78
#define ADDR_GameFileService_TaskEntry 0x081e459c
#define ADDR_GameFileService_RunWorker 0x081e527c
#define ADDR_GameFileService_Perform 0x081e48f4
#define ADDR_GameFileService_Append 0x081e45b0
#define ADDR_GameFileService_Drain 0x081e42c8
#define ADDR_GameFileService_PerformOpen 0x081e4ea0
#define ADDR_GameFileService_PerformClose 0x081e4fd8
#define ADDR_GameFileService_PerformRead 0x081e4f3c
#define ADDR_GameFileService_PerformWrite 0x081e4ff4
#define ADDR_GameFileService_PerformSeek 0x081e4f80
#define ADDR_GameFileService_PerformDelete 0x081e5168
#define ADDR_GameFileService_PerformRename 0x081e520c
#define ADDR_GameFileService_PerformList 0x081e5358
#define ADDR_GameFileService_AddHandle 0x081e50e4
#define ADDR_GameFileService_GetHandle 0x081e5258
#define ADDR_GameFileService_RemoveHandle 0x081e52f0
#define ADDR_GameRuntime_AppendCompletedRequest 0x0828cc60
#define ADDR_GameRuntime_TakeCompletedRequests 0x0828ceb0
#define ADDR_GameSaveTable_Get 0x0813ecb4
#define ADDR_GameSaveTable_Open 0x0813ed94
#define ADDR_GameSaveTable_Retain 0x0813ef30
#define ADDR_GameSaveTable_Read 0x0813ed24
#define ADDR_GameSaveTable_Write 0x0813ef80
#define ADDR_GameSaveTable_FlushAll 0x0813e9a8
#define ADDR_GameSaveSlot_IsBound 0x08292428
#define ADDR_GameSaveSlot_Read 0x0813ea34
#define ADDR_GameSaveSlot_Store 0x0813eabc
#define ADDR_GameSaveSlot_Release 0x0813ec20
#define ADDR_GameSaveSlot_Flush 0x0813eb34
#define ADDR_GameSaveSlot_Free 0x0813ebc8
#define ADDR_Bitmap_LoadResource 0x08103984
#define ADDR_GraphicsContext_ConstructDefault 0x0825b008
#define ADDR_GraphicsContext_FillRect 0x0825ae34
#define ADDR_GraphicsContext_ClearRect 0x0825ad4c
#define ADDR_GraphicsContext_SetColor 0x08259b28
#define ADDR_GraphicsContext_SetBackgroundColor 0x08259b20
#define ADDR_GraphicsContext_SetFont 0x0825aba8
#define ADDR_GraphicsContext_SetTextPen 0x0825aba0
#define ADDR_GraphicsContext_SetDrawMode 0x082596fc
#define ADDR_GraphicsContext_DrawUtf8 0x08259648
#define ADDR_GraphicsContext_MeasureUtf8 0x08259d10
#define ADDR_GraphicsContext_CopyRect 0x08259568
#define ADDR_GraphicsContext_InitWithBitmap 0x0825aefc
#define ADDR_GraphicsContext_InitFromBitmapResource 0x0825af2c
#define ADDR_GraphicsContext_SetOrigin 0x0825aef0
#define ADDR_Bitmap_Construct 0x08103e8c
#define ADDR_Bitmap_Destroy 0x08103ee0
#define ADDR_GraphicsContext_Destroy 0x0825b038
#define ADDR_Font_Construct 0x0826c0f4
#define ADDR_GraphicsContext_GetFontAscent 0x08298784
#define ADDR_GraphicsContext_GetFontDescent 0x082987b8
#define ADDR_Surface_FillRect 0x08069a5c
#define ADDR_RGB565_BlendFixed 0x0826a9a8
#define ADDR_ArtworkReflection_GenerateRGB565 0x0826a7c0
#define ADDR_BitmapCache_Init 0x08119d54
#define ADDR_BitmapCache_Load 0x0828fdc0
#define ADDR_BitmapCache_GetRect 0x0828fd74
#define ADDR_BitmapCache_Draw 0x0828fbb0
#define ADDR_Rect_Offset 0x08262f00
#define ADDR_Rect_Intersect 0x08262b54
#define ADDR_ArtworkRenderer_Draw 0x08271fa0
#define ADDR_CoverFlowEdges_SmoothCenter 0x0826a44c
#define ADDR_GameGraphics_GetCurrentPixelBuffer 0x082c7f14
#define ADDR_IAP_VerifyDeviceSignatureRSA 0x0812dca8
#define ADDR_IAP_VerifyDeviceSignatureAlternate 0x081592fc
#define ADDR_IAP_HandleDeviceAuthenticationSignature 0x081892b8
#define ADDR_IAP_SetAccessoryAuthState 0x08185e18
#define ADDR_Wheel_HandleInterrupt 0x0805f688
#define ADDR_Wheel_RunTask 0x0805f738
#define ADDR_Wheel_DispatchButtonChanges 0x0807443c
#define ADDR_Input_HandleButton 0x08056900
#define ADDR_HoldSwitch_NotifyRawChange 0x080c75f8
#define ADDR_Event_Signal 0x0802cfa8
#define ADDR_IRQ_RestoreState 0x083924d8
#define ADDR_IRQ_DisableSave 0x083924e0
#define ADDR_InputWheel_AccumulateMotion 0x08093718
#define ADDR_InputWheel_ConvertPosition 0x080a7bec
#define ADDR_InputWheel_ConsumeMotion 0x080dae38
#define ADDR_InputWheel_ConsumeMotionAndExtra 0x080d1a78
#define ADDR_InputWheel_NotifyMotion 0x0807c9f4
#define ADDR_InputWheel_PostNotification 0x08056a10
#define ADDR_EventManager_PostMessage 0x08121764
#define ADDR_EventManager_ForwardMessage 0x081213a4
#define ADDR_EventManager_Init 0x0812113c
#define ADDR_EventManager_AllocEvent 0x081212b8
#define ADDR_EventManager_FreeEvent 0x081214d4
#define ADDR_EventManager_CountFreeEvents 0x08121734
#define ADDR_InputEvent_BuildName 0x0826c1b8
#define ADDR_MediaListModel_GetIndexedProperty 0x0816a04c
#define ADDR_MediaListModel_Destroy 0x0816d508
#define ADDR_MediaPlayer_GetPosition 0x0816f704
#define ADDR_MediaPlayer_InitializePlaybackList 0x08170b6c
#define ADDR_MediaPlayer_IsStopped 0x08172568
#define ADDR_MediaPlayer_TogglePlayback 0x08170304
#define ADDR_MediaPlayer_GetInstance 0x08172824
#define ADDR_MediaPlayer_ClearPreparedTrack 0x0817190c
#define ADDR_MediaPlayer_RebuildArtwork 0x08170238
#define ADDR_MediaPlayer_GetCurrentItem 0x0816f790
#define ADDR_MediaPlayer_GetItemAtIndex 0x0816ec38
#define ADDR_MediaPlayer_SetCurrentIndex 0x0816f058
#define ADDR_MediaPlayer_PlayIndex 0x08171e7c
#define ADDR_MediaPlayer_RemoveEntryAtIndex 0x081700bc
#define ADDR_MediaPlayer_IsRepeatAll 0x08172ec8
#define ADDR_MediaPlayer_IsRepeatOne 0x08172efc
#define ADDR_MediaPlayer_Seek 0x08172148
#define ADDR_MediaPlayer_ApplyEQSettingsId 0x081723e0
#define ADDR_MediaPlayer_GetEQSettingsId 0x081722f4
#define ADDR_MediaPlayer_GetEQMenuIndex 0x0816e9b4
#define ADDR_MediaPlayer_Stop 0x081721a0
#define ADDR_MediaPlayer_ResetPlaybackList 0x0816fbf0
#define ADDR_App_SyncCurrentItem 0x08108d38
#define ADDR_NowPlayingModel_Get 0x081ed74c
#define ADDR_NowPlayingModel_RefreshCurrentItem 0x081ef48c
#define ADDR_NowPlayingModel_GetProperty 0x081eda2c
#define ADDR_NowPlayingModel_HandleEvent 0x081efe74
#define ADDR_MediaListController_PlayRow 0x0820f358
#define ADDR_AudiobookChapters_SelectRow 0x08231bb0
#define ADDR_MediaPlayer_CreatePlaybackList 0x081719d4
#define ADDR_MediaItem_IsValid 0x0829a7dc
#define ADDR_MediaItem_InitFromRecord 0x08266d20
#define ADDR_MediaItem_Destroy 0x08266da0
#define ADDR_MediaItem_Assign 0x08266da4
#define ADDR_MediaItem_HasRecordFlag8F01 0x0829af84
#define ADDR_MediaItem_HasKind8 0x0829a9d4
#define ADDR_MediaItem_HasKind8062 0x0829aad8
#define ADDR_MediaItem_HasKind200000 0x0829afb4
#define ADDR_MediaItem_GetPath 0x0829a1d8
#define ADDR_MediaItem_GetFileSizeBytes 0x0829a27c
#define ADDR_MediaItem_GetBitrateKbps 0x0829aa34
#define ADDR_MediaItem_GetYear 0x0829a2a8
#define ADDR_MediaItem_GetRating 0x0829aa08
#define ADDR_MediaItem_GetTrackNumber 0x082995ec
#define ADDR_MediaItem_GetDiscNumber 0x082990f0
#define ADDR_MediaItem_GetPlayCount 0x0829afe8
#define ADDR_MediaItem_GetSkipCount 0x0829b014
#define ADDR_MediaItem_GetChapterCount 0x08299644
#define ADDR_MediaItem_GetChapterStartMs 0x08299b50
#define ADDR_MediaItem_GetChapterTitleAt 0x08299820
#define ADDR_MediaItem_GetEQSettingsId 0x0829911c
#define ADDR_MediaItem_GetChapterIndexAt 0x08299c88
#define ADDR_MediaItem_GetFileType 0x0829a0d0
#define ADDR_MediaItem_GetComment 0x0829ab98
#define ADDR_MediaItem_GetCategory 0x0829ab14
#define ADDR_MediaItem_GetDescription 0x082993cc
#define ADDR_MediaItem_GetShowName 0x082992c4
#define ADDR_MediaItem_GetDateAdded 0x0829aa60
#define ADDR_MediaItem_GetDateModified 0x0829adf8
#define ADDR_MediaItem_GetLastPlayed 0x082991a4
#define ADDR_MediaItem_GetLastSkipped 0x08299504
#define ADDR_MediaItem_GetReleaseDate 0x0829957c
#define ADDR_MediaListView_IsValid 0x0829d030
#define ADDR_MediaListView_GetCount 0x0829c858
#define ADDR_MediaListView_RemoveEntryAtIndex 0x0827a8c8
#define ADDR_MediaEntry_Remove 0x0803db30
#define ADDR_MediaList_NotifyChanged 0x080564b8
#define ADDR_PlaybackList_GetEntryByIndex 0x0829cab8
#define ADDR_MediaList_InvalidateOrderCache 0x080d1928
#define ADDR_MediaList_CopyEntryAtPosition 0x0803bbf0
#define ADDR_MediaList_GetOrderSortFields 0x080da038
#define ADDR_SongRecord_CompareField 0x0803a63c
#define ADDR_MediaList_GetEntryByOrderIndex 0x08047870
#define ADDR_MediaList_GetEntryOrderIndex 0x080497ac
#define ADDR_MediaList_BuildOrderIndex 0x080d438c
#define ADDR_MediaList_ResolveOrderMode 0x080a9ab0
#define ADDR_MediaListController_AddSelectionToOnTheGo 0x0820f8d4
#define ADDR_MediaListController_DispatchAction 0x0820f5a4
#define ADDR_MediaListController_HandleShowContextualMenu 0x082102b0
#define ADDR_MediaListController_MapVisibleRow 0x082573a8
#define ADDR_NestedPlaylists_MapVisibleRow 0x0822fa4c
#define ADDR_OnTheGo_AddBrowserSelection 0x081095f8
#define ADDR_MediaListSelection_SelectArtworkSource 0x08169c98
#define ADDR_MediaListSelection_SetList 0x08169db4
#define ADDR_MediaListSelection_GetInstance 0x08169178
#define ADDR_MediaListSelection_SetTitle 0x08169c7c
#define ADDR_ArtworkSession_Stop 0x0820a76c
#define ADDR_ArtworkSession_Construct 0x08216a90
#define ADDR_ArtworkSession_Destroy 0x08222298
#define ADDR_ArtworkSession_Attach 0x0821674c
#define ADDR_ArtworkSession_SetFormat 0x08216a24
#define ADDR_ArtworkSession_BindQuery 0x082168b0
#define ADDR_ArtworkSession_Center 0x082221f0
#define ADDR_ArtworkWindow_Center 0x0820b29c
#define ADDR_ArtworkSession_Start 0x08221d80
#define ADDR_PlaybackArtwork_GetImage 0x082168f0
#define ADDR_ArtworkSource_CreateProvider 0x08138b84
#define ADDR_AlbumArtworkProvider_Create 0x08140030
#define ADDR_Genius_GetInstance 0x08121a4c
#define ADDR_Genius_MapVisibleRow 0x08122180
#define ADDR_Genius_GetItem 0x08121a7c
#define ADDR_ITunesDB_ImportLibrary 0x08058afc
#define ADDR_ITunesDB_ImportTrackDataset 0x0808a488
#define ADDR_ITunesDB_ImportCachedString 0x080cdee0
#define ADDR_ITunesDB_SkipImportBytes 0x08069000
#define ADDR_TrackData_AcquireStringCaches 0x08051370
#define ADDR_TrackData_ReleaseStringCaches 0x08061284
#define ADDR_TrackData_DestroyStorage 0x080cfaf4
#define ADDR_TrackData_Destroy 0x0803e58c
#define ADDR_MediaBrowser_GetOwner 0x08046ee0
#define ADDR_MediaBrowser_GetCurrentList 0x08046e00
#define ADDR_MediaBrowser_CreateRoot 0x0804d6e4
#define ADDR_MediaBrowser_CreateChild 0x0804d80c
#define ADDR_MediaBrowser_EvaluateEntryFilters 0x080d7a98
#define ADDR_MediaBrowser_CopyMatchingEntries 0x0803bf78
#define ADDR_MediaBrowser_InvalidateEntryMatches 0x0804ec24
#define ADDR_MediaBrowser_ClearArtists 0x0804e850
#define ADDR_MediaBrowser_ClearAlbums 0x0804e794
#define ADDR_MediaBrowser_ClearSongs 0x0804ebe8
#define ADDR_MediaBrowser_GetRecordArtworkId 0x080464b0
#define ADDR_MediaBrowser_AccumulateArtistGroup 0x080c6bf0
#define ADDR_MediaBrowser_AccumulateAlbumGroup 0x080bf518
#define ADDR_MediaBrowser_BuildArtists 0x08048fe8
#define ADDR_MediaBrowser_BuildSongs 0x0807b858
#define ADDR_MediaBrowser_ReadArtistName 0x08047b54
#define ADDR_MediaBrowser_ReadArtistJumpLabel 0x08047bc8
#define ADDR_MediaBrowser_ReadAlbumName 0x08047a34
#define ADDR_MediaBrowser_SelectAlbumIndex 0x0805b528
#define ADDR_MediaBrowser_GetOrderMode 0x0804af48
#define ADDR_AlbumGroupVector_Append 0x083d8b74
#define ADDR_LibraryBrowser_Create 0x0825012c
#define ADDR_LibraryBrowser_InitBorrowed 0x08133be8
#define ADDR_LibraryBrowser_Destroy 0x08133c30
#define ADDR_LibraryBrowser_CopyAlbumArtistSubtitle 0x08133168
#define ADDR_LibraryModelRef_Init 0x0839576c
#define ADDR_LibraryModelRef_InitCopy 0x083957d4
#define ADDR_LibraryModelRef_Assign 0x08395818
#define ADDR_LibraryModelRef_Release 0x083937cc
#define ADDR_LibraryModelRef_Get 0x083ccd1c
#define ADDR_Library_LoadDatabases 0x0805d6c8
#define ADDR_MediaPlayer_GetReadAheadBytes 0x0822c92c
#define ADDR_FixedPool_Create 0x08053074
#define ADDR_Heap_GetTotalBytes 0x0809cc58
#define ADDR_BlockCache_Get 0x08180830
#define ADDR_BlockCache_GetBlockBytes 0x0817fd4c
#define ADDR_BlockCache_GetTotalBlocks 0x08180628
#define ADDR_BlockCache_Grow 0x080ca238
#define ADDR_BlockCache_Trim 0x080cd16c
#define ADDR_BlockCache_PostRebalance 0x0817fd68
#define ADDR_BlockCache_PostWaitPending 0x0817fdd0
#define ADDR_BlockCache_PostClientRequest 0x0817fd9c
#define ADDR_BlockCache_AddBlocks 0x081804e8
#define ADDR_BlockCache_PushFreeBlock 0x081801a4
#define ADDR_BlockCache_GetAssignedBlockCount 0x08180690
#define ADDR_BlockCache_CountPendingClients 0x08180840
#define ADDR_BlockCache_IncrementPendingClientCount 0x08180700
#define ADDR_BlockCache_DecrementPendingClientCount 0x08180644
#define ADDR_BlockList_Erase 0x083cc960
#define ADDR_BlockCacheClient_SetDesiredBytes 0x081f1f50
#define ADDR_BlockCacheClient_ReturnBlock 0x081f2888
#define ADDR_BlockCacheClient_CancelPending 0x081f2988
#define ADDR_EAppBlockManager_HandleMessage 0x0819d67c
#define ADDR_EAppBlocks_Acquire 0x0819d870
#define ADDR_EAppBlocks_Release 0x0819d40c
#define ADDR_MediaStream_Seek 0x081db470
#define ADDR_MP4Parser_SeekToTime 0x081bd1cc
#define ADDR_MP4Parser_SeekAudioTrack 0x081bb188
#define ADDR_MP4Parser_SeekPrimaryTrack 0x081bb678
#define ADDR_MP4Parser_ReadSamples 0x081bb0f8
#define ADDR_MP4Parser_ReadAudioTrackSamples 0x081bbed0
#define ADDR_MP4Parser_ReadPrimaryTrackSamples 0x081bc644
#define ADDR_MP4Parser_LoadSampleSizeWindow 0x081bac44
#define ADDR_MP4Parser_LoadSampleToChunkWindow 0x081bab1c
#define ADDR_MP4Parser_LoadChunkOffsetWindow 0x081baaa0
#define ADDR_MP4Parser_ReadBigEndianWords 0x081b940c
#define ADDR_MP4Parser_LoadTrackEdit 0x081ba664
#define ADDR_MP4Parser_LoadPrimaryTrackEdit 0x081ba708
#define ADDR_Runtime_UDiv64 0x08024024
#define ADDR_PMU_SetBacklightEnabled 0x08363c70
#define ADDR_PMU_ReadMemory 0x082dc2e8
#define ADDR_PMU_WriteMemory 0x082dc5e4
#define ADDR_Power_EnterLightSleep 0x0805d0c0
#define ADDR_PowerController_HandleIdleSleep 0x0819b28c
#define ADDR_PowerController_HandleRequestedSleep 0x0819bb18
#define ADDR_PowerController_Get 0x0819aeec
#define ADDR_Battery_GetChargeFraction 0x08046624
#define ADDR_BatteryGauge_GetLevel 0x081dc03c
#define ADDR_PowerController_PostEvent 0x081d41a0
#define ADDR_PowerController_NotifyGameStarted 0x0819ae98
#define ADDR_PowerController_HandleGameStarted 0x0819c6fc
#define ADDR_PowerController_HandleIdleTimeout 0x0819c62c
#define ADDR_PowerController_SetSleepAfterWake 0x0819bc44
#define ADDR_PowerController_ObserveInput 0x0819b10c
#define ADDR_PowerController_HandleWheelEvent 0x0819b5b8
#define ADDR_TaskMessage_PostBegin 0x0803d124
#define ADDR_TaskMessage_PostEndAfter 0x0803d064
#define ADDR_System_SaveAll 0x080638d8
#define ADDR_Disk_GetPowerState 0x082b3630
#define ADDR_StorageArbiter_Get 0x0807bf3c
#define ADDR_StorageArbiter_Activate 0x0815dde4
#define ADDR_StorageArbiter_SetOwner 0x0815dc00
#define ADDR_StorageArbiter_Request 0x0815dcec
#define ADDR_StorageArbiter_Release 0x0815dc58
#define ADDR_DiskClient_OnGranted 0x081d8248
#define ADDR_DiskClient_Yield 0x081d8258
#define ADDR_ScsiDiskClient_Create 0x08130648
#define ADDR_ScsiDiskClient_Request 0x08130620
#define ADDR_HardwareEvent_Post 0x0807c5b4
#define ADDR_HardwareEvent_PostWithArgument 0x0807c5bc
#define ADDR_HardwareEvent_MapSource 0x080d9f18
#define ADDR_HardwareEvent_Dispatch 0x081dc214
#define ADDR_App_HandleDiskModeRequest 0x0810b6e8
#define ADDR_App_PollDiskModeEntry 0x081089ec
#define ADDR_App_EnterDiskMode 0x08106854
#define ADDR_App_ExitDiskMode 0x0810b5e0
#define ADDR_System_PrepareDiskMode 0x0805df34
#define ADDR_SystemUI_DispatchSystemEvent 0x0810acd0
#define ADDR_App_HandleDiskGranted 0x08108bf0
#define ADDR_App_HandleReloadTimer 0x081084d0
#define ADDR_App_StartMusicLoading 0x0810b974
#define ADDR_App_PollMusicLoading 0x0810a514
#define ADDR_MusicLoadingTask_Create 0x081aec48
#define ADDR_MusicLoadingTask_Run 0x081aec04
#define ADDR_System_CheckVolumeOrRestart 0x081dc1d0
#define ADDR_ResourceBank_InitOverrides 0x08111f00
#define ADDR_ResourceBank_SetResource 0x08111e20
#define ADDR_ResourceBank_GetResource 0x08111d74
#define ADDR_ResourceBank_FirstResource 0x081120f4
#define ADDR_ResourceBank_NextResource 0x08112000
#define ADDR_ResourceBank_RemoveHeapResource 0x08111fbc
#define ADDR_ResourceBank_Destroy 0x081124a4
#define ADDR_ResourceOverrides_Find 0x0812252c
#define ADDR_ResourceOverrides_EraseEntry 0x08122574
#define ADDR_ResourceNames_FindOrInsertSlot 0x083d242c
#define ADDR_ResourceBlockList_AppendBorrowedBlock 0x0817a7ec
#define ADDR_ResourceBlockList_Load 0x0817a82c
#define ADDR_ResourceDtTm_CopyDefault 0x0827b844
#define ADDR_ResourceService_GetString 0x08268d94
#define ADDR_ResourceService_GetBitmap 0x08268d84
#define ADDR_UI_GetResourceBank 0x0819579c
#define ADDR_UI_SetResourceBank 0x081957c0
#define ADDR_UI_GetResourceService 0x08268d34
#define ADDR_UI_InitResourceProviders 0x081c7e68
#define ADDR_UI_InitializeViewTemplates 0x081af5b4
#define ADDR_ResourceName_GetId 0x08286e74
#define ADDR_Heap_AllocateZeroed 0x0805232c
#define ADDR_Heap_Free 0x080521c0
#define ADDR_Runtime_OperatorNew 0x082a17e0
#define ADDR_Mutex_Lock 0x08258b34
#define ADDR_Mutex_Unlock 0x08258b38
#define ADDR_Mutex_Init 0x08258b3c
#define ADDR_HoldSwitch_GetLogicalState 0x0807c010
#define ADDR_HoldSwitch_ReadHardwareBit 0x082dc010
#define ADDR_Platform_InitMicrosecondCounter 0x083619c4
#define ADDR_SystemUI_HandleHoldChange 0x08108c14
#define ADDR_Platform_HasRegionalVolumeLimit 0x0804b17c
#define ADDR_File_OpenLegacyPath 0x0804f784
#define ADDR_File_ReadLegacyHandle 0x0804f838
#define ADDR_File_WriteLegacyHandle 0x0804ff68
#define ADDR_File_CloseLegacyHandle 0x0804f6e4
#define ADDR_File_SeekLegacyHandle 0x0804f88c
#define ADDR_File_SeekLegacyHandle64 0x0826f1b0
#define ADDR_File_GetLegacyHandleSize 0x0829be28
#define ADDR_BufferedFile_GetPosition 0x08037f98
#define ADDR_Preferences_LoadFile 0x0809c69c
#define ADDR_Preferences_LoadAndMigrate 0x080587f4
#define ADDR_Startup_CopyImageToRuntimeRegions 0x22004700
#define ADDR_Platform_ImportBootSysInfo 0x08360668
#define ADDR_Platform_GetImportedSysInfo 0x08360620
#define ADDR_Platform_GetHeapRegion 0x0804b448
#define ADDR_Memory_InitPlatformHeapBounds 0x0806bf34
#define ADDR_System_CreateAndStartConfiguredTasks 0x0805d5f4
#define ADDR_Preferences_FlushIfDirty 0x08063760
#define ADDR_Preferences_MarkDirty 0x080537bc
#define ADDR_Preferences_SetClickerRoute 0x0805bef0
#define ADDR_Preferences_GetClickerRoute 0x080465fc
#define ADDR_PreferencesService_Get 0x08167a54
#define ADDR_File_DeleteLegacyPath 0x0804f99c
#define ADDR_Runtime_Abort 0x0802606c
#define ADDR_Runtime_ReportUncaughtException 0x0825de34
#define ADDR_DirectoryIterator_Init 0x081e57dc
#define ADDR_DirectoryIterator_Next 0x081e54e8
#define ADDR_DirectoryIterator_Destroy 0x081e5928
#define ADDR_Directory_CreateWithParents 0x08090840
#define ADDR_NativePath_InitEmpty 0x0826fcc4
#define ADDR_NativePath_Destroy 0x0826fcf4
#define ADDR_NativePath_InitFromString 0x0826fc94
#define ADDR_File_RenameNativePaths 0x080e9d4c
#define ADDR_Thread_Create 0x0804b4f4
#define ADDR_Thread_GetCurrentRecord 0x0804b744
#define ADDR_Scheduler_YieldOrDelay 0x0804ba3c
#define ADDR_IRQ_Entry 0x22003080
#define ADDR_IRQ_Dispatch 0x22008150
#define ADDR_Clock_GetMicroseconds 0x0804b7ac
#define ADDR_RTC_ReadDateTime 0x08083ba4
#define ADDR_RTC_GetZoneOffsets 0x080a072c
#define ADDR_TimeZone_GetOffsetMinutes 0x080f4ed0
#define ADDR_TimeZone_IsDSTActive 0x080f5080
#define ADDR_SetWarmBootModeTag 0x082b3a44
#define ADDR_ResetSocAndWait 0x0835e934
#define ADDR_Timer_Stop 0x081219d0
#define ADDR_FileHandle_Open 0x0826f7bc
#define ADDR_FileHandle_OpenString 0x0826f884
#define ADDR_FileHandle_OpenNative 0x0826ec48
#define ADDR_FileHandle_IsOpen 0x0829be9c
#define ADDR_FileHandle_Read 0x0826eecc
#define ADDR_FileHandle_Write 0x0826f3b0
#define ADDR_File_StoreLegacyHandleSize 0x0806d198
#define ADDR_Preferences_GetLanguage 0x0816748c
#define ADDR_App_StoreBrightness 0x081069b0
#define ADDR_Settings_HandleAction 0x0821daf4
#define ADDR_Settings_CollectPropertyResources 0x08295174
#define ADDR_SettingsModel_GetInstance 0x081e0ff4
#define ADDR_SettingsModel_Construct 0x081e4010
#define ADDR_SettingsModel_ToggleClicker 0x081e300c
#define ADDR_SettingsModel_NotifyClicker 0x081e22f8
#define ADDR_SettingsModel_GetProperty 0x081e11f4
#define ADDR_SettingsModel_GetIndexedProperty 0x081e30ac
#define ADDR_SettingsModel_ToggleMainMenuItem 0x081e3460
#define ADDR_SettingsModel_ToggleMusicMenuItem 0x081e36ac
#define ADDR_SettingsModel_ApplyMainMenuPreferences 0x081e2a50
#define ADDR_SettingsModel_ApplyMusicMenuPreferences 0x081e2e48
#define ADDR_SettingsModel_ResetMainMenu 0x081e1024
#define ADDR_SettingsModel_ResetMusicMenu 0x081e1134
#define ADDR_SettingsModel_LoadPreferences 0x081e3f00
#define ADDR_SettingsModel_GetEQPresetPreview 0x081e0f74
#define ADDR_SettingsModel_GetEQPresetName 0x081e0fc0
#define ADDR_MainMenuSettings_HandleSelection 0x081d2200
#define ADDR_MusicMenuSettings_HandleSelection 0x081dd6c8
#define ADDR_SettingsModel_ResetAllSettings 0x081e1e6c
#define ADDR_SettingsController_ConfirmResetAll 0x08212820
#define ADDR_Settings_SetEQPreset 0x0805c0b8
#define ADDR_SettingsModel_SelectEQMenuIndex 0x081e4008
#define ADDR_SettingsModel_SetPreviewIndex 0x081e11d4
#define ADDR_SettingsEQ_HandleAction 0x081345e4
#define ADDR_SettingsEQ_ApplySelection 0x081347b0
#define ADDR_SettingsEQ_ShowPreview 0x08134690
#define ADDR_SettingsMainMenu_HandleAction 0x081d2144
#define ADDR_SettingsEQ_HandleEvent 0x08398e78
#define ADDR_SettingsEQ_SelectCurrentRow 0x08134750
#define ADDR_SettingsBacklight_SelectCurrentRow 0x08100d88
#define ADDR_SettingsMusicMenu_HandleAction 0x081dd60c
#define ADDR_SettingsModel_AdjustBrightness 0x081e2ee8
#define ADDR_CppString_Init 0x083cf82c
#define ADDR_CppString_InitBytes 0x083cf87c
#define ADDR_CppString_Destroy 0x083cf7d4
#define ADDR_String_CompareCString 0x0838886c
#define ADDR_StringInputStream_Init 0x0829e7b8
#define ADDR_StringInputStream_Destroy 0x0829e81c
#define ADDR_NativeString_InitEmpty 0x0826de38
#define ADDR_NativeString_Destroy 0x0826de7c
#define ADDR_NativeString_GetUtf8 0x0829bac0
#define ADDR_NativeString_InitUtf8 0x0826dcfc
#define ADDR_NativeString_AssignUtf8 0x0826cd94
#define ADDR_NativeString_AssignUtf16 0x0826cfa0
#define ADDR_SongText_CompareUtf16 0x0803a574
#define ADDR_StringCache_ReadToken 0x080a94ec
#define ADDR_StringCache_ReleaseToken 0x080bb0c8
#define ADDR_StringCache_GetTokenView 0x08093598
#define ADDR_PodcastEpisodes_DispatchAction 0x0823077c
#define ADDR_PodcastEpisodes_BuildAndShowContextualMenu 0x082309d0
#define ADDR_ITunesUEpisodes_DispatchAction 0x08230ad8
#define ADDR_ITunesUEpisodes_BuildAndShowContextualMenu 0x08230d2c
#define ADDR_Audiobooks_BuildAndShowContextualMenu 0x0822ac40
#define ADDR_Movies_DispatchAction 0x0821d860
#define ADDR_TVEpisodes_DispatchAction 0x0822ad5c
#define ADDR_NestedVideoPlaylists_DispatchAction 0x08254c00
#define ADDR_MediaListController_ShowContextMenuDefault 0x08210674
#define ADDR_MenuList_RebuildVisibleItems 0x081946fc
#define ADDR_MenuList_SetItemTable 0x08194d1c
#define ADDR_MenuItems_RegisterResourceType 0x08194df0
#define ADDR_UI_ShowContextMenuItems 0x08129d78
#define ADDR_ContextMenuModel_Get 0x081debd0
#define ADDR_ContextMenuModel_SetItems 0x081decd0
#define ADDR_ContextMenuModel_GetRememberedIndex 0x081dec00
#define ADDR_UI_SetScreenState 0x08176af8
#define ADDR_ContextMenuModel_GetProperty 0x081dec28
#define ADDR_ContextMenuModel_GetIndexedProperty 0x081ded30
#define ADDR_UI_LoadLayoutInstances 0x081d64cc
#define ADDR_ContextMenuController_DispatchAction 0x081de6f8
#define ADDR_ContextMenuController_ForwardAction 0x081dea30
#define ADDR_ScreenController_SwitchLayoutDefault 0x08129994
#define ADDR_UI_GetLayoutManager 0x081747ec
#define ADDR_MenuList_QueryProperty 0x08194c2c
#define ADDR_ControllerRegistry_InvokeControllerFactory 0x081d58dc
#define ADDR_ControllerRegistry_InvokeScreenFactory 0x081d5a40
#define ADDR_UI_FindServiceById 0x081c7d44
#define ADDR_UI_GetRootView 0x08113600
#define ADDR_UI_GetSelectionModel 0x0817dde4
#define ADDR_UI_GetControllerRegistry 0x081d5950
#define ADDR_UI_CreateScreenController 0x081d59e0
#define ADDR_UI_RegisterControllerFactories 0x081d5ac8
#define ADDR_UI_RegisterViewFactory 0x081d70d8
#define ADDR_UI_LoadLayoutEventTables 0x081d61b0
#define ADDR_UI_GetLayoutEventTable 0x081d63e0
#define ADDR_UIEventMap_ReadStream 0x082a191c
#define ADDR_ScreenController_CreateNamed 0x0812a0d8
#define ADDR_ScreenController_HandleEvent 0x0812a39c
#define ADDR_ResourceScreen_Activate 0x08398954
#define ADDR_ResourceScreen_Deactivate 0x08398b6c
#define ADDR_MenuItem_SetVisible 0x08194ff0
#define ADDR_Source_LoadDescriptors 0x0817a50c
#define ADDR_TextMenuRow_Initialize 0x0820f2c4
#define ADDR_ContextMenuItems_Append 0x08162390
#define ADDR_ContextMenuItems_Init 0x081624e8
#define ADDR_ContextMenuItems_Destroy 0x0816250c
#define ADDR_WheelEvent_DecodeMotion 0x08289490
#define ADDR_ButtonEvent_GetUIKeyCode 0x0829b978
#define ADDR_WheelAccumulator_Init 0x081b0efc
#define ADDR_WheelAccumulator_ConsumeMotion 0x081b0e4c
#define ADDR_WheelAccumulator_Reset 0x081b0e7c
#define ADDR_WheelAccumulator_AccumulateMotion 0x081b0d4c
#define ADDR_WheelAccumulator_SetThreshold 0x081b0d40
#define ADDR_WheelAccumulator_SetMaxMultiplier 0x081b0d38
#define ADDR_WheelAccumulator_IsSlowTurn 0x081b0ebc
#define ADDR_WheelAccumulator_IsFastTurn 0x081b0e9c
#define ADDR_View_InitOptionsFromDescriptor 0x0829b090
#define ADDR_View_ResolveBounds 0x08264f9c
#define ADDR_View_Construct 0x08265bf8
#define ADDR_View_BindGraphicsContext 0x08264bdc
#define ADDR_View_GetLocalContentRect 0x08299014
#define ADDR_View_GetLocalBounds 0x08298f04
#define ADDR_View_GetWindowBounds 0x08298fe8
#define ADDR_View_BindGraphicsContextToBounds 0x082654e8
#define ADDR_View_InvalidateRect 0x082655a0
#define ADDR_View_GetWindow 0x08299080
#define ADDR_Window_InvalidateRect 0x082843ac
#define ADDR_View_DrawBackgroundAndBorder 0x08263530
#define ADDR_View_Draw 0x0826576c
#define ADDR_View_DrawClipped 0x08265388
#define ADDR_View_HandleModelNotification 0x08265a2c
#define ADDR_ProgressBar_CreateFromResource 0x081d5fac
#define ADDR_UI_RegisterBuiltinViewFactories 0x081d7110
#define ADDR_ProgressBar_RefreshFromModel 0x082053a4
#define ADDR_ProgressBar_DrawMarkers 0x0820504c
#define ADDR_ProgressBar_FillWidthForValue 0x08205028
#define ADDR_ProgressBar_ComputeFillWidth 0x082051a0
#define ADDR_ProgressBar_LoadBitmapCaches 0x08205224
#define ADDR_ProgressBar_Construct 0x08205ddc
#define ADDR_ProgressBarStyle8_Construct 0x0820996c
#define ADDR_ProgressBar_Draw 0x08205530
#define ADDR_ProgressBarStyle8_Draw 0x0820938c
#define ADDR_TextViewTimer_ObserveInput 0x0819cdd8
#define ADDR_TextView_CreateFromDescriptor 0x0817b238
#define ADDR_TextView_DrawTextWithColor 0x08287218
#define ADDR_MarqueeTextView_Draw 0x081a8dc8
#define ADDR_CoverFlowView_UpdateActiveState 0x0813818c
#define ADDR_GLAlbumArtView_UpdateActiveState 0x081641b8
#define ADDR_VideosMenu_InitController 0x0822a8ec
#define ADDR_MainMediaMenu_DispatchAction 0x0822a780
#define ADDR_MainMediaMenu_DispatchCommonAction 0x08223d20
#define ADDR_MusicMenu_ConfigureLibraryBrowser 0x08223ab8
#define ADDR_NowPlaying_DispatchAction 0x08212dbc
#define ADDR_NowPlaying_BuildAndShowContextualMenu 0x0821505c
#define ADDR_SongList_DispatchAction 0x08218b24
#define ADDR_SongList_BuildAndShowContextualMenu 0x08219478
#define ADDR_NestedPlaylists_DispatchAction 0x0822ea1c
#define ADDR_NestedPlaylists_BuildAndShowContextualMenu 0x0822fdb4
#define ADDR_Genius_DispatchAction 0x0821c0e4
#define ADDR_Genius_BuildAndShowContextualMenu 0x0821d0cc
#define ADDR_ArtistsController_ShowContextMenu 0x0821fe9c
#define ADDR_UI_InitializeEventTimer 0x0810635c
#define ADDR_Timer_Start 0x08121268
#define ADDR_UITask_Create 0x081391b8
#define ADDR_UITask_Main 0x08139118
#define ADDR_Timer_SetPeriod 0x0812195c
#define ADDR_Controller_ExecuteNavigatorAction 0x08129998
#define ADDR_Navigator_PopTop 0x082896b8
#define ADDR_Navigator_PopToScreen 0x08289728
#define ADDR_Navigator_PopToMainScreen 0x08289714
#define ADDR_Navigator_PopToPixoMainScreen 0x08289768
#define ADDR_ExtrasMenu_DispatchAction 0x0811a664
#define ADDR_UI_ConstructScreenDefinition 0x0813ca44
#define ADDR_UI_LoadScreenLayouts 0x0813c91c
#define ADDR_ScreenDefinition_Destruct 0x0813cb3c
#define ADDR_Layout_Destruct 0x081d6b7c
#define ADDR_SettingsMenuListController_Deactivate 0x083a4a90
#define ADDR_GamesMenuController_Deactivate 0x0839736c
#define ADDR_ResourceScreen_HandleEvent 0x08398bdc
#define ADDR_ScreenController_DispatchNamedEvent 0x0812a070
#define ADDR_BrightnessController_HandleEvent 0x081ec9d4
#define ADDR_BrightnessController_HandleWheel 0x081ec864
#define ADDR_WheelInput_QuantizeMotion 0x08289794
#define ADDR_WheelEvent_GetSpeed 0x082894e8
#define ADDR_ScreenController_PopSelf 0x081034f0
#define ADDR_ResourceService_GetIndexedResource 0x08138dd8
#define ADDR_MenuList_GetRowProperty 0x08194ecc
#define ADDR_MenuList_HandleNotification 0x08195120
#define ADDR_MenuItems_RegisterAll 0x081951cc
#define ADDR_BitmapView_LoadBitmap 0x08100020
#define ADDR_Object_GetInterface 0x0826c594
#define ADDR_UI_TransitionToLayout 0x0817518c
#define ADDR_Layout_GetKind 0x081d6ad4
#define ADDR_UI_AnimateInstanceIn 0x081774c8
#define ADDR_UI_AnimateInstanceOut 0x08177d84
#define ADDR_UI_AnimateSharedInstance 0x08176b28
#define ADDR_UI_InstanceEnterStyle 0x08179d04
#define ADDR_UI_InstanceLeaveStyle 0x0817a040
#define ADDR_UI_SlideLayerIn 0x081786c8
#define ADDR_UI_SlideLayerOut 0x08178924
#define ADDR_UI_MoveViewToLayer 0x08179684
#define ADDR_UI_HoldViewInBackgroundLayer 0x08179dbc
#define ADDR_UI_SlideStatusBarChildren 0x08178e2c
#define ADDR_UI_SlideLayerFromPane 0x081790ec
#define ADDR_UI_GetCompositorLayer 0x0826dfec
#define ADDR_SportTimerChosenDispatcher_Activate 0x0821abc4
#define ADDR_UI_EmitControllerEvent 0x0812a150
#define ADDR_ScreenController_ShowLayout 0x08398a94
#define ADDR_ControllerLayoutCache_GetOrInsert 0x0813ca2c
#define ADDR_UI_CreateTemplateFromResource 0x081df580
#define ADDR_UI_ConstructLayout 0x081d6adc
#define ADDR_UIntPointerMap_GetOrInsert 0x083d25fc
#define ADDR_View_FindViewById 0x08292580
#define ADDR_UI_FindTemplateView 0x081af514
#define ADDR_UI_DisplayPreparedLayout 0x08174464
#define ADDR_UI_ApplyInstanceResources 0x081781c0
#define ADDR_View_ApplyPropertyBindings 0x08175b7c
#define ADDR_Navigator_Get 0x08104d00
#define ADDR_NavigatorProperties_Has 0x080f70bc
#define ADDR_NavigatorProperties_GetInteger 0x080f7348
#define ADDR_NavigatorProperties_SetInteger 0x080f6f2c
#define ADDR_NavigatorProperties_SetString 0x080f6f80
#define ADDR_NavigatorProperties_Remove 0x080f7310
#define ADDR_Navigator_Push 0x0810514c
#define ADDR_UI_GetRootOwner 0x0813e204
#define ADDR_TextView_DrawString 0x082883f8
#define ADDR_TextView_DrawTextLayout 0x082872f8
#define ADDR_MenuList_RestoreState 0x0827d77c
#define ADDR_MenuList_SelectRow 0x0827d1b4
#define ADDR_UI_ResolveTemplateView 0x0817481c
#define ADDR_CapacityView_CreateFromDescriptor 0x081d587c
#define ADDR_CapacityView_LoadSegments 0x081370f4
#define ADDR_CapacityView_Draw 0x08137738
#define ADDR_BitmapView_Draw 0x0810012c
#define ADDR_MenuRowBitmapView_Draw 0x081911f8
#define ADDR_KeyboardView_Draw 0x08204170
#define ADDR_KeyboardView_DrawKeyRow 0x08203f6c
#define ADDR_KeyboardView_DrawKey 0x08203e8c
#define ADDR_OptionBarView_Draw 0x081e06d0
#define ADDR_OptionBarView_SetSelected 0x081e0688
#define ADDR_CalendarView_DrawClipped 0x081363fc
#define ADDR_CalendarDayCell_Draw 0x081bff58
#define ADDR_GLAlbumArtView_Draw 0x08163efc
#define ADDR_GLAlbumArtView_GetImage 0x08163d64
#define ADDR_GLAlbumArtAnimation_RenderCover 0x0815cd4c
#define ADDR_UI_ProcessMessage 0x081215d8
#define ADDR_UI_RunMessageLoop 0x08288a94
#define ADDR_Model_Notify 0x08268cf8
#define ADDR_SelectionGradient_GetBitmap 0x0809353c
#define ADDR_MediaArtworkProvider_Get 0x081adae8
#define ADDR_MediaArtworkProvider_GetSlotArtwork 0x081ac834
#define ADDR_AlarmsService_Get 0x080ff998
#define ADDR_AlarmsService_SelectSleepTimer 0x080fdf5c
#define ADDR_USB_HandleSetupPacket 0x0828a404
#define ADDR_USB_DispatchControlRead 0x0828a7d4
#define ADDR_USB_HandleVendorDeviceRead 0x0828a27c
#define ADDR_USB_DispatchNoDataRequest 0x0828a780
#define ADDR_USB_HandleAppleVendorCommand 0x0828a054
#define ADDR_USB_GetControlWriteBuffer 0x0828a8f8
#define ADDR_USB_StartControlTransfer 0x0828a3b4
#define ADDR_USB_RunDeviceLoop 0x0828afd8
#define ADDR_USB_ContinueControlWrite 0x0828acd4
#define ADDR_USB_SelectConfigurationOrder 0x0828b668
#define ADDR_USBDeviceTask_Run 0x0813e598
#define ADDR_USB_IsAccessoryMode 0x0813e560
#define ADDR_SCSI_AcquireMedium 0x080ed98c
#define ADDR_ThreeBandEQ_vtable 0x089ae0c4
#define ADDR_AudioOutputQueue 0x22010398
#define ADDR_EQ_Presets 0x089d4b58
#define ADDR_EQ_PresetIds 0x083e4110
#define ADDR_DisplayLayerOrders 0x083f35ec
#define ADDR_DisplayBlendRegisters 0x083f35e0
#define ADDR_FairPlay_StorageHandle 0x089d2bd4
#define ADDR_GameAPI_ModuleRegistry 0x08a182e0
#define ADDR_GameImport_EndOfListName 0x089b10a0
#define ADDR_GameSettings_Table 0x089d29a0
#define ADDR_GameSettings_TimeFormatTable 0x089d297c
#define ADDR_GameSettings_LanguageTable 0x089d2d24
#define ADDR_GameBacklight 0x089d2970
#define ADDR_GameList_vtable 0x08989c54
#define ADDR_EGL_ProcedureTable 0x083e3ed0
#define ADDR_GameAPI_MiscTBD_Module 0x08a182e0
#define ADDR_GameAPI_DebugUtil_Module 0x08a18358
#define ADDR_GameAPI_MemoryAlloc_Module 0x08a183b0
#define ADDR_GameAPI_InputEvents_Module 0x08a18400
#define ADDR_GameAPI_Settings_Module 0x08a1844c
#define ADDR_GameAPI_AsyncFileIO_Module 0x08a18494
#define ADDR_GameAPI_SoundEffect_Module 0x08a1851c
#define ADDR_GameAPI_OpenGLES_Module 0x08a18608
#define ADDR_GameAPI_Audio_Module 0x08a188f4
#define ADDR_GameAPI_Metadata_Module 0x08a18a10
#define ADDR_GameAPI_Users_Module 0x08a18cf8
#define ADDR_Bitmap_vtable 0x08989298
#define ADDR_Palette_White 0x089d4f18
#define ADDR_Palette_LightGray 0x089d4f1c
#define ADDR_Palette_Gray 0x089d4f20
#define ADDR_Palette_DarkGray 0x089d4f24
#define ADDR_Palette_Black 0x089d4f28
#define ADDR_EventManager 0x089d38c0
#define ADDR_WheelController 0x3c200000
#define ADDR_WheelState 0x089d2b8c
#define ADDR_ButtonKeyCodes 0x089d2ec5
#define ADDR_UIKeyNames_Button 0x083e46d0
#define ADDR_UIKeyNames_Remote 0x083e4710
#define ADDR_UIKeyNames_Video 0x083e47f8
#define ADDR_UIKeyNames_Image 0x083e4868
#define ADDR_UIKeyNames_Audio 0x083e48b0
#define ADDR_UIKeyNames_Mikey 0x083e4958
#define ADDR_InputEventNameWords 0x089d2e48
#define ADDR_NowPlayingModel_Instance 0x089d48e0
#define ADDR_MediaPlayer_vtable 0x08990da4
#define ADDR_BrowserRegistry 0x08a1a6dc
#define ADDR_BlockCache 0x089d380c
#define ADDR_MainHeap 0x089d2c78
#define ADDR_ATA_Identify 0x08ae087c
#define ADDR_MediaStream_vtable 0x08996bdc
#define ADDR_MP4Parser_vtable 0x08994a58
#define ADDR_I2C0Controller 0x3c600000
#define ADDR_ClockRegister_PowerGate1 0x3c50004c
#define ADDR_StorageArbiter 0x08a7e8f8
#define ADDR_DiskClient 0x08a7e920
#define ADDR_DiskClient_vtable 0x08996b78
#define ADDR_ScsiDiskClient_vtable 0x0898c96c
#define ADDR_MusicLoadingTask_vtable 0x08994174
#define ADDR_App 0x089d2cb4
#define ADDR_PowerController_Instance 0x089d4e18
#define ADDR_PowerController_Transitions 0x083ed4ec
#define ADDR_PowerController_StateHandlers 0x083ed3bc
#define ADDR_ResourceNameMap 0x08ae02c4
#define ADDR_ResourceRomImage 0x084020dc
#define ADDR_ClickerRoute 0x089d2bdc
#define ADDR_TimeZones 0x083eb96c
#define ADDR_TimeZoneOffsets 0x083eb8ec
#define ADDR_IRQ_Nesting 0x22008af4
#define ADDR_MainMenuSettingsRows 0x08a815f0
#define ADDR_MusicMenuSettingsRows 0x08a81898
#define ADDR_ScreenController_vtable 0x0899d3fc
#define ADDR_View_vtable 0x089ad8ec
#define ADDR_SleepTimerChoices 0x089d3980
#define ADDR_TemplateView_vtable 0x08997824
#define ADDR_ProgressBar_vtable 0x0899a3dc
#define ADDR_ProgressBarStyle8_vtable 0x0899ab84
#define ADDR_SportTimerChosenDispatcher_vtable 0x089a72dc
#define ADDR_SettingsMenuListController_vtable 0x089a3ae0
#define ADDR_ExtrasMenuController_vtable 0x0899cb30
#define ADDR_GamesMenuController_vtable 0x0899c798
#define ADDR_ArtistsController_vtable 0x089a40e4
#define ADDR_AlbumsController_vtable 0x089a3514
#define ADDR_NestedPlaylistsController_vtable 0x089a6380
#define ADDR_NestedVideoPlaylistsController_vtable 0x089a6bfc
#define ADDR_ITunesUEpisodesController_vtable 0x089a67ec
#define ADDR_AudiobooksController_vtable 0x089a59d4
#define ADDR_MoviesController_vtable 0x089a3980
#define ADDR_TVEpisodesController_vtable 0x089a5b4c
#define ADDR_Navigator 0x08a822cc
#define ADDR_NavigatorProperties 0x08a82268
#define ADDR_TextView_vtable 0x089af07c
#define ADDR_SelectedRowTextView_vtable 0x08993504
#define ADDR_MenuList_vtable 0x08992c60
#define ADDR_TextMenuRow_vtable 0x0899b904
#define ADDR_MenuListModel_vtable 0x08992c70
#define ADDR_GLAlbumArtView_vtable 0x08990618
#define ADDR_SelectionBar_RamGradient 0x089d4ee8
#define ADDR_SelectionBar_RamHighlight 0x089d4eec
#define ADDR_SelectionGradient_Bitmap 0x08a82324
#define ADDR_MediaArtworkProvider 0x089d4c74

#define SITE_ArtworkDatabase_Import_call 0x0804d458
#define SITE_ArtworkDatabase_ReadImageThumbs_call 0x08074f34
#define SITE_ArtworkDatabase_FindById_PhotoAlbum_call 0x08074d44
#define SITE_ArtworkDatabase_FindByDbid_0804581c_call 0x0804581c
#define SITE_ArtworkDatabase_FindByDbid_08046494_call 0x08046494
#define SITE_AudioOutputQueue_SilenceFill_call 0x22006e80
#define SITE_AudioOutputQueue_Get_call 0x0818cd7c
#define SITE_EQ_Process_slot 0x089ae0e0
#define SITE_EQ_Reset_slot 0x089ae0d4
#define SITE_EQ_SetPreset_Load_call 0x082735dc
#define SITE_EQ_SetRate_Load_call 0x08273610
#define SITE_EQ_DspCount_word 0x082735c8
#define SITE_EQ_TrackGuard_word 0x08173b74
#define SITE_EQ_TrackPreset_call 0x08173b80
#define SITE_EQ_SettingsIdToDspPreset_entry 0x080b28a0
#define SITE_EQ_ApplySettingsId_slot 0x08990e44
#define SITE_EQ_SettingsCount_word 0x080bb498
#define SITE_EQ_ProviderCount_word 0x081e1ae8
#define SITE_EQ_MenuIds_literal 0x0807bfb0
#define SITE_EQ_SettingsIds_literal 0x080bb4ac
#define SITE_EQ_PreviewTable_literal 0x081e0fa8
#define SITE_EQ_NameTable_literal 0x081e0ff0
#define SITE_EQ_ResourceTable_literal 0x082955dc
#define SITE_VolumePolicy_RegionTest_word 0x0804745c
#define SITE_Volume_EUOffCeiling_word 0x081558f4
#define SITE_Volume_EUDefaultLimit_word 0x08155af0
#define SITE_VolumePolicy_WarningPolicyTest_word 0x08155a90
#define SITE_Volume_StartupClampTest_word 0x08058a30
#define SITE_VolumeExposure_TrackFinishTest_word 0x081732dc
#define SITE_MediaPlayer_PostVolumeExposureLimitEvent_word 0x080cfe74
#define SITE_DisplayPipe_Init_SetGammaTables_call 0x081441bc
#define SITE_GameInput_ReadWheel_entry 0x082bda94
#define SITE_GameManifest_InitReader_call 0x080f9418
#define SITE_GamesService_BuildList_entry 0x080f543c
#define SITE_GamesService_ScanBuiltInRoot_call 0x080f5474
#define SITE_GamesService_ScanDiskRoot_call 0x080f5488
#define SITE_GameFiles_BuildPath_entry 0x08096f58
#define SITE_GamesService_PlayedString_call 0x080f64a0
#define SITE_GamesService_PlayedTimesString_call 0x080f64ec
#define SITE_GamesService_ResetAfterGame_entry 0x080f6918
#define SITE_GamesService_GetArtworkId_entry 0x080f5af0
#define SITE_GamesService_GetArtworkPath_entry 0x080f6264
#define SITE_GamesService_GetArtworkStorage_entry 0x080f55cc
#define SITE_GamesService_ResetAfterGame_call 0x080f5974
#define SITE_GraphicsContext_FillRect_call 0x0825ae9c
#define SITE_GraphicsContext_ClearRect_call 0x0825adb4
#define SITE_ArtworkReflection_Blend_call 0x0826a890
#define SITE_IAP_VerifyDeviceSignatureRSA_call 0x081893a8
#define SITE_IAP_VerifyDeviceSignatureAlternate_call 0x081893fc
#define SITE_HoldSwitch_NotifyRawChange_call 0x08055b44
#define SITE_InputWheel_ConsumeMotionAndExtra_entry 0x080d1a78
#define SITE_MediaListModel_IndexedProperty_slot 0x08990cdc
#define SITE_NowPlayingModel_Property_slot 0x08998708
#define SITE_MediaListModel_Destroy_slot 0x08990c5c
#define SITE_MediaListModel_Destroy_call 0x0816d4fc
#define SITE_MediaPlayer_ToggleStopped_call 0x08170360
#define SITE_AlbumSubtitle_slot 0x0898cbf4
#define SITE_Albums_CancelAppend_call 0x0821c004
#define SITE_MediaListController_DispatchAction_entry 0x0820f5a4
#define SITE_MediaListController_HandleShowContextualMenu_entry 0x082102b0
#define SITE_MediaList_OnTheGoAdd_call 0x0820f9cc
#define SITE_ITunesDB_ImportTrackDataset_call 0x08058f1c
#define SITE_ITunesDB_AlbumArtist_word 0x0808adcc
#define SITE_ITunesDB_SortArtist_word 0x0808add0
#define SITE_ITunesDB_SortAlbumArtist_word 0x0808ade8
#define SITE_TrackData_DestroyStorage_call 0x0803e650
#define SITE_MediaBrowser_BuildArtists_entry 0x08048fe8
#define SITE_MediaBrowser_ReadArtistName_entry 0x08047b54
#define SITE_MediaBrowser_ReadArtistJumpLabel_entry 0x08047bc8
#define SITE_MediaBrowser_ArtistFilterCompare_call 0x080d7bd8
#define SITE_AlbumGroup_literal 0x08048fe4
#define SITE_LibraryBrowser_Destroy_entry 0x08133c30
#define SITE_MediaListSelection_SelectArtworkSource_call 0x08169e30
#define SITE_AlbumArtworkProvider_Create_call 0x08138c34
#define SITE_BlockCache_ImportCost_word 0x0805ddd4
#define SITE_BlockCache_ReserveNative_word 0x0805ddd8
#define SITE_BlockCache_FloorTest_word 0x0805dddc
#define SITE_BlockCache_FloorSet_word 0x0805dde0
#define SITE_MediaPlayer_ReadAheadMinTest_word 0x0822c944
#define SITE_MediaPlayer_ReadAheadMinSet_word 0x0822c948
#define SITE_MediaList_GetEntryOrderIndex_entry 0x080497ac
#define SITE_TrackData_ArtworkId_word 0x0808a868
#define SITE_FixedPool_SlotRoundAdd_word 0x080530b4
#define SITE_FixedPool_SlotRoundMask_word 0x080530b8
#define SITE_EAppBlocks_Acquire_call 0x0819d6d0
#define SITE_EAppBlocks_Release_call 0x0819d6d8
#define SITE_MediaStream_Seek_slot 0x08996c00
#define SITE_MP4Parser_SeekToTime_slot 0x08994a68
#define SITE_MP4Parser_SeekAudioTrack_entry 0x081bb188
#define SITE_MP4Parser_SeekEditEndTest_word 0x081bb5e8
#define SITE_MP4Parser_ReadEditStartTest_word 0x081bc0f4
#define SITE_MP4Parser_ReadEditEndTest_word 0x081bc114
#define SITE_Sleep_Idle_call 0x0819b32c
#define SITE_Sleep_Request_call 0x0819bba8
#define SITE_Preferences_FlushIfDirty_call 0x0806392c
#define SITE_DiskClient_Yield_slot 0x08996ba0
#define SITE_App_HandleDiskModeRequest_call 0x0810ad54
#define SITE_SystemUI_HandleHoldChange_call 0x0810af14
#define SITE_MusicLoadingTask_Run_slot 0x0899417c
#define SITE_DiskModeExit_PopToMainScreen_call 0x0810b65c
#define SITE_ResourceBlockList_AppendBorrowedBlock_call 0x0817a88c
#define SITE_ResourceBank_InitOverrides_call 0x081c7ea8
#define SITE_ResourceBank_NextTemplate_call 0x081af75c
#define SITE_Preferences_LoadFile_call 0x08058808
#define SITE_HeapRegionBase_literal 0x0804b478
#define SITE_RuntimeHeapBase_literal 0x0807c048
#define SITE_Panic_entry 0x0803930c
#define SITE_Abort_entry 0x0802606c
#define SITE_RegionalVolumeResult_word 0x0804b188
#define SITE_Runtime_ReportUncaughtException_entry 0x0825de34
#define SITE_IRQ_Dispatch_entry 0x22008150
#define SITE_HoldSwitch_GetLogicalState_entry 0x0807c010
#define SITE_Settings_Action_slot 0x089a3bf4
#define SITE_Settings_SecondaryAction_slot 0x089a7c3c
#define SITE_SettingsMainMenu_Action_slot 0x089a00f4
#define SITE_SettingsMainMenu_SecondaryAction_slot 0x08996280
#define SITE_SettingsMusicMenu_Action_slot 0x089a05bc
#define SITE_SettingsMusicMenu_SecondaryAction_slot 0x08997094
#define SITE_Settings_Property_slot 0x08997b60
#define SITE_Settings_IndexedProperty_slot 0x08997b7c
#define SITE_EQ_PreviewCount_word 0x081e0f74
#define SITE_EQ_NameCount_word 0x081e0fc0
#define SITE_EQ_ResourceCount_word 0x08295420
#define SITE_MainMenu_Index_call 0x081d2214
#define SITE_MusicMenu_Index_call 0x081dd6dc
#define SITE_MainMenu_Toggle_call 0x081d225c
#define SITE_MusicMenu_Toggle_call 0x081dd724
#define SITE_MusicMenu_Apply_call 0x081e3f48
#define SITE_SettingsModel_ResetMainMenu_entry 0x081e1024
#define SITE_SettingsModel_ResetMusicMenu_entry 0x081e1134
#define SITE_SettingsModel_ResetAllSettings_call 0x08212844
#define SITE_TrackCount_0803b2d8_word 0x0803b2d8
#define SITE_TrackCount_0803bf9c_word 0x0803bf9c
#define SITE_TrackCount_0803c9bc_word 0x0803c9bc
#define SITE_TrackCount_0804789c_word 0x0804789c
#define SITE_TrackCount_0804793c_word 0x0804793c
#define SITE_TrackCount_08047970_word 0x08047970
#define SITE_TrackCount_08047c7c_word 0x08047c7c
#define SITE_TrackCount_08048e08_word 0x08048e08
#define SITE_TrackCount_08048e48_word 0x08048e48
#define SITE_TrackCount_08049070_word 0x08049070
#define SITE_TrackCount_08049368_word 0x08049368
#define SITE_TrackCount_08049938_word 0x08049938
#define SITE_TrackCount_080499c4_word 0x080499c4
#define SITE_TrackCount_0804a180_word 0x0804a180
#define SITE_TrackCount_08051eb8_word 0x08051eb8
#define SITE_TrackCount_080562f0_word 0x080562f0
#define SITE_TrackCount_080586c0_word 0x080586c0
#define SITE_TrackCount_080587b0_word 0x080587b0
#define SITE_TrackCount_08059788_word 0x08059788
#define SITE_TrackCount_0805cb04_word 0x0805cb04
#define SITE_TrackCount_0805cf70_word 0x0805cf70
#define SITE_TrackCount_0806294c_word 0x0806294c
#define SITE_TrackCount_080637d4_word 0x080637d4
#define SITE_TrackCount_08063834_word 0x08063834
#define SITE_TrackCount_0807b8b8_word 0x0807b8b8
#define SITE_TrackCount_0807b8f0_word 0x0807b8f0
#define SITE_TrackCount_0807c87c_word 0x0807c87c
#define SITE_TrackCount_08082248_word 0x08082248
#define SITE_TrackCount_08082280_word 0x08082280
#define SITE_TrackCount_080822a8_word 0x080822a8
#define SITE_TrackCount_0808232c_word 0x0808232c
#define SITE_TrackCount_08089058_word 0x08089058
#define SITE_TrackCount_0808905c_word 0x0808905c
#define SITE_TrackCount_0808909c_word 0x0808909c
#define SITE_TrackCount_080890a4_word 0x080890a4
#define SITE_TrackCount_080890a8_word 0x080890a8
#define SITE_TrackCount_080890b0_word 0x080890b0
#define SITE_TrackCount_08092e24_word 0x08092e24
#define SITE_TrackCount_08092e28_word 0x08092e28
#define SITE_TrackCount_080931b0_word 0x080931b0
#define SITE_TrackCount_080931b4_word 0x080931b4
#define SITE_TrackCount_0809cdc0_word 0x0809cdc0
#define SITE_TrackCount_0809cdc4_word 0x0809cdc4
#define SITE_TrackCount_0809cdd4_word 0x0809cdd4
#define SITE_TrackCount_0809cddc_word 0x0809cddc
#define SITE_TrackCount_0809cde0_word 0x0809cde0
#define SITE_TrackCount_0809cde8_word 0x0809cde8
#define SITE_TrackCount_0809f69c_word 0x0809f69c
#define SITE_TrackCount_080b3460_word 0x080b3460
#define SITE_TrackCount_080b3dcc_word 0x080b3dcc
#define SITE_TrackCount_080b3dec_word 0x080b3dec
#define SITE_TrackCount_080bf914_word 0x080bf914
#define SITE_TrackCount_080bf9b8_word 0x080bf9b8
#define SITE_TrackCount_080d16b4_word 0x080d16b4
#define SITE_TrackCount_080d16b8_word 0x080d16b8
#define SITE_TrackCount_080d43cc_word 0x080d43cc
#define SITE_TrackCount_081087e8_word 0x081087e8
#define SITE_TrackCount_0810897c_word 0x0810897c
#define SITE_TrackCount_0816ebc4_word 0x0816ebc4
#define SITE_TrackCount_0829c884_word 0x0829c884
#define SITE_MenuItem_SetVisible_branch 0x0827e3a8
#define SITE_ProgressBar_Draw_slot 0x0899a450
#define SITE_ProgressBarStyle8_Draw_slot 0x0899abf8
#define SITE_ProgressBar_DrawMarkers_slot 0x0899a514
#define SITE_ProgressBarStyle8_DrawMarkers_slot 0x0899acbc
#define SITE_ProgressBar_MarkerBlit_call 0x08205180
#define SITE_PodcastEpisodes_CancelAppend_call 0x08230a10
#define SITE_PodcastEpisodes_Action_slot 0x089a67a0
#define SITE_ITunesUEpisodes_CancelAppend_call 0x08230d6c
#define SITE_ITunesUEpisodes_Action_slot 0x089a6900
#define SITE_Audiobooks_CancelAppend_call 0x0822ac80
#define SITE_Audiobooks_Action_slot 0x089a5ae8
#define SITE_Movies_ShowContextMenu_slot 0x089a3ac8
#define SITE_Movies_Action_slot 0x089a3a94
#define SITE_TVEpisodes_ShowContextMenu_slot 0x089a5c94
#define SITE_TVEpisodes_Action_slot 0x089a5c60
#define SITE_NestedVideoPlaylists_Action_slot 0x089a6d10
#define SITE_UI_LoadLayoutEvents_call 0x081d63f8
#define SITE_UI_TemplateEventsGet_call 0x081d6218
#define SITE_UI_LayoutEventsGet_call 0x081d62b4
#define SITE_UI_ControllerEventsGet_call 0x081d634c
#define SITE_ScreenController_Deactivate_slot 0x0899d4ec
#define SITE_SettingsMenuListController_Deactivate_slot 0x089a3bd0
#define SITE_GamesMenuController_Deactivate_slot 0x0899c888
#define SITE_NowPlaying_Action_slot 0x0899be50
#define SITE_NowPlaying_SecondaryAction_slot 0x089a2e00
#define SITE_Music_Action_slot 0x089a940c
#define SITE_Music_SecondaryAction_slot 0x089a507c
#define SITE_Videos_Action_slot 0x089a9da8
#define SITE_Videos_SecondaryAction_slot 0x089a59b4
#define SITE_Main_Action_slot 0x089a4c9c
#define SITE_Main_SecondaryAction_slot 0x089a8fa4
#define SITE_TextView_DrawColor_call 0x082872a8
#define SITE_View_BackgroundFill_call 0x082636b4
#define SITE_MarqueeTextView_DrawColor_call 0x081a8f0c
#define SITE_UI_DisplayPreparedLayout_call 0x08175208
#define SITE_ContextMenuModel_Property_slot 0x08997648
#define SITE_ContextMenuModel_IndexedProperty_slot 0x08997664
#define SITE_ContextMenu_ScreenActionCompare_call 0x081de7b0
#define SITE_NavigatorAction_PopToScreen_call 0x08129bc4
#define SITE_CoverFlow_ClearColor_call 0x08138304
#define SITE_AlbumArt_ClearColor_call 0x08164230
#define SITE_SongList_CancelAppend_call 0x082195ec
#define SITE_NestedPlaylists_CancelAppend_call 0x0822ffb8
#define SITE_Genius_CancelAppend_call 0x0821d26c
#define SITE_NowPlaying_CancelAppend_call 0x0821526c
#define SITE_Artists_CancelAppend_call 0x0821fee8
#define SITE_SongList_DispatchAction_entry 0x08218b24
#define SITE_NestedPlaylists_DispatchAction_entry 0x0822ea1c
#define SITE_Genius_DispatchAction_entry 0x0821c0e4
#define SITE_UITask_StackShift_word 0x081391dc
#define SITE_Extras_Action_slot 0x0899cc44
#define SITE_Artists_Action_slot 0x089a41f8
#define SITE_Albums_Action_slot 0x089a3628
#define SITE_TextView_DrawTextWithColor_call 0x08288514
#define SITE_TextView_DrawTextLayout_call 0x082884a8
#define SITE_TextView_DrawString_entry 0x082883f8
#define SITE_BitmapView_DrawColor_call 0x0810020c
#define SITE_BitmapView_TileBlit_call 0x08100278
#define SITE_BitmapView_Blit_call 0x081002e4
#define SITE_MenuRowBitmapView_TileBlit_call 0x0819136c
#define SITE_MenuRowBitmapView_Blit_call 0x081913b8
#define SITE_KeyboardView_DrawColor_call 0x08203f2c
#define SITE_OptionBarView_DrawColor_call 0x081e0930
#define SITE_CalendarDayCell_TopMarkerBlit_call 0x081c0064
#define SITE_CalendarDayCell_BottomMarkerBlit_call 0x081c010c
#define SITE_CalendarDayCell_DrawColor_call 0x081c0170
#define SITE_CalendarView_DrawColor_call 0x081365e4
#define SITE_GLAlbumArtView_Draw_slot 0x0899068c
#define SITE_AlbumArt_RendererDraw_call 0x0815cd54
#define SITE_AlbumArt_EdgeLines_call 0x0815cd7c
#define SITE_SelectionBar_StyleBranch_word 0x08263704
#define SITE_SelectionBar_Highlight_literal 0x08263e10
#define SITE_SelectionBar_Gradient_literal 0x08263e14
#define SITE_MediaArtworkProvider_GetSlotArtwork_entry 0x081ac834
#define SITE_UI_ProcessMessage_call 0x08288b98
#define SITE_USB_ContinueControlWrite_call 0x0828b1e0
#define SITE_USB_HandleVendorDeviceRead_call 0x0828a820
#define SITE_USB_DispatchNoDataRequest_call 0x0828a458
#define SITE_USB_GetControlWriteBuffer_call 0x0828a434

#define ArtworkDatabase_image_count 0x8
#define ArtworkDatabase_first_image 0xc
#define ArtworkDatabase_last_image 0x10
#define ArtworkDatabase_image_pool 0x2c
#define ArtworkImage_next 0x0
#define ArtworkImage_id 0xc
#define ArtworkImage_dbid 0x10
#define ArtworkImage_thumb_count 0x22
#define ArtworkImage_thumbs 0x24
#define ArtworkReader_database 0x10
#define ArtworkThumb_next 0x0
#define ArtworkThumb_image 0x4
#define ArtworkThumb_format 0x8
#define ArtworkThumb_dimensions 0xc
#define ArtworkThumb_padding 0x10
#define ArtworkThumb_offset 0x18
#define ArtworkThumb_size 0x1c
#define ArtworkThumb_file 0x20
#define ArtworkThumb_bytes 0x24
#define AudioOutputQueue_underruns_since_reset 0x28
#define AudioOutputQueue_underruns 0x2c
#define ThreeBandEQ_enable_method 0x8
#define ThreeBandEQ_is_enabled_method 0xc
#define ThreeBandEQ_reset_method 0x10
#define ThreeBandEQ_set_sample_rate_method 0x14
#define ThreeBandEQ_output_frames_method 0x18
#define ThreeBandEQ_process_method 0x1c
#define ThreeBandEQ_sample_rate 0x4
#define ThreeBandEQ_enabled 0x8
#define ThreeBandEQ_stages 0xc
#define ThreeBandEQ_preset 0xb4
#define ThreeBandEQ_bytes 0xb8
#define EQStage_a1 0x0
#define EQStage_a2 0x4
#define EQStage_b0 0x8
#define EQStage_b1 0xc
#define EQStage_b2 0x10
#define EQStage_history 0x14
#define EQStage_bypass 0x34
#define EQStage_bytes 0x38
#define EQPreset_name 0x0
#define EQPreset_preview 0x4
#define EQPreset_bytes 0x8
#define AudioEffectsContext_eq 0x18034
#define AudioEffectsContext_speed 0x180ec
#define AudioEffectsContext_post_gain_q15 0x2a170
#define AudioEffectsContext_input_frames 0x2a174
#define AudioEffectsContext_source_rate 0x2a178
#define AudioEffectsContext_source_bits 0x2a17c
#define AudioEffectsContext_source_channels 0x2a180
#define AudioEffectsContext_effects 0x2a190
#define AudioEffectsContext_output_rate 0x2a184
#define AudioEffectsContext_output_bits 0x2a188
#define AudioEffectsContext_output_channels 0x2a18c
#define AudioMixer_effects 0x20
#define AudioService_effects 0x8
#define Volume_apply_method 0x54
#define Volume_get_method 0x5c
#define Volume_ceiling_method 0x60
#define Volume_set_limit_method 0x84
#define Volume_current 0x58
#define Volume_limit 0x5c
#define Volume_track_adjustment 0x60
#define AudioOutputDevice_set_level_method 0x14
#define AudioOutputDevice_set_sound_check_method 0x24
#define MediaTrack_state 0x0
#define MediaTrack_item 0x4
#define MediaTrack_record 0xc
#define MediaTrack_playback_settings 0x20
#define DisplayPipe_control 0x8
#define DisplayPipe_video_mode 0x28
#define DisplayPipe_video_strides 0x2c
#define DisplayPipe_video_source_start 0x30
#define DisplayPipe_video_source_end 0x34
#define DisplayPipe_video_plane0 0x38
#define DisplayPipe_video_plane1 0x3c
#define DisplayPipe_video_plane2 0x40
#define DisplayPipe_video_plane3 0x44
#define DisplayPipe_video_scale 0x4c
#define DisplayPipe_video_position 0x50
#define DisplayPipe_video_size 0x54
#define DisplayPipe_slot0 0x58
#define DisplayPipe_slot_step 0x18
#define DisplayPipe_order 0xd4
#define DisplayPipe_gamma_red 0x400
#define DisplayPipe_gamma_green 0x800
#define DisplayPipe_gamma_blue 0xc00
#define DisplaySlot_stride 0x0
#define DisplaySlot_format 0x4
#define DisplaySlot_source 0x8
#define DisplaySlot_size 0xc
#define DisplaySlot_position 0x14
#define BootDescriptor_flags 0x10
#define BootDescriptor_sysinfo_magic 0x18
#define BootDescriptor_sysinfo 0x1c
#define GameImageHeader_magic 0x0
#define GameImageHeader_version 0x4
#define GameImageHeader_entry_count 0x8
#define GameImageHeader_imports 0x10
#define GameImageHeader_entries 0x14
#define GameImageHeader_bytes 0x28
#define GameFrameInput_state 0x0
#define GameFrameInput_events 0x30
#define GameFrameInput_completed_requests 0x2c
#define GameFrameInput_pixels 0x38
#define GameFrameInput_bytes 0x100
#define GameFrameOutput_state 0x0
#define GameFrameOutput_bytes 0xf8
#define GameInputEvent_button 0x0
#define GameInputEvent_type 0x1
#define GameInputEvent_timestamp 0x4
#define GameInputEvent_next 0x8
#define GameInputEvent_bytes 0xc
#define GameSurface_color_mode 0x64
#define GameSurface_pixels 0x68
#define GameSurface_width 0x94
#define GameSurface_height 0x98
#define GameRuntime_frame_input 0x0
#define GameRuntime_frame_output 0x100
#define GameRuntime_identifier 0x24c
#define GameRuntime_storage 0x254
#define GameRuntime_entry_count 0x268
#define GameRuntime_init_entry 0x26c
#define GameRuntime_shutdown_entry 0x270
#define GameRuntime_frame_entry 0x27c
#define GameRuntime_game_heap 0x294
#define GameRuntime_file_service 0x298
#define GameRuntime_completed_requests 0x29c
#define GameRuntime_completed_tail 0x2a0
#define GameManifestReader_manifest_path 0x0
#define GameManifestReader_signature_path 0x4
#define GameManifestReader_volume 0x8
#define GameManifestReader_bytes 0xc
#define GameListEntry_name_is_string 0x0
#define GameListEntry_name 0x4
#define GameListEntry_identifier 0x14
#define GameListEntry_artwork_path 0x20
#define GameListEntry_storage 0x28
#define GameListEntry_version 0x2c
#define GameListEntry_user_data_path 0x34
#define GameListEntry_name_string_id 0x48
#define GameListEntry_bytes 0x50
#define GameList_count 0x4
#define GameList_storage 0x8
#define GameList_iterators 0xc
#define GameList_element_bytes 0x10
#define GameList_capacity 0x14
#define GameList_growth 0x18
#define GameList_scratch 0x1c
#define GameList_sorted 0x20
#define GameList_keep_sorted 0x21
#define GameList_comparator 0x24
#define GameList_owns_entries 0x28
#define GameList_bytes 0x2c
#define GamesService_list 0x28
#define GamesService_users 0x54
#define GamesService_play_counts 0x70
#define GamesService_selected_index 0x8c
#define GamesService_list_built 0x90
#define GamesService_game_active 0x100
#define GamesService_artwork_started 0x210
#define GamesService_artwork_center 0x214
#define GamesService_artwork_pending_rows 0x218
#define GameAPIModule_name 0x0
#define GameAPIModule_interface_count 0x20
#define GameAPIModule_function_count 0x24
#define GameAPIModule_next 0x28
#define GameAPIModule_functions 0x2c
#define GameAPIInterface_id 0x0
#define GameAPIInterface_function_count 0x10
#define GameAPIInterface_bytes 0x14
#define GameImport_name 0x0
#define GameImport_interface_id 0x20
#define GameImport_function_count 0x30
#define GameImport_next 0x34
#define GameImport_stubs 0x38
#define GameFileRequest_next 0x0
#define GameFileRequest_type 0x10
#define GameFileRequest_completion 0x14
#define GameFileRequest_offset 0x18
#define GameFileRequest_whence 0x1c
#define GameFileRequest_buffer 0x20
#define GameFileRequest_length 0x24
#define GameFileRequest_result 0x28
#define GameFileRequest_transferred 0x2c
#define GameFileRequest_size 0x30
#define GameFileRequest_handle 0x34
#define GameSaveSlot_path 0x0
#define GameSaveSlot_data 0x8
#define GameSaveSlot_bytes 0xc
#define GameSaveSlot_position 0x10
#define GameSaveSlot_references 0x14
#define GameSaveSlot_dirty 0x18
#define GameSaveSlot_lock 0x1c
#define GameSaveSlot_record_bytes 0x24
#define GameAudioBridge_player 0x0
#define GameAudioBridge_playback_changed 0xd
#define GameSound_data 0x4
#define GameSound_byte_count 0x8
#define GameSound_flag_c 0xc
#define GameSound_sample_rate 0x10
#define GameSound_channels 0x14
#define GameSound_bits_per_sample 0x18
#define GameSound_volume 0x1c
#define GameSound_speed 0x20
#define GameSound_pan 0x24
#define GameSound_lead_in_ms 0x28
#define GameSound_tail_ms 0x2c
#define GameSound_gain 0x30
#define GameSound_voice 0x34
#define GameSound_play_count 0x38
#define GameSound_state 0x3d
#define GameSound_flags 0x3e
#define GameSound_next 0x40
#define GameSound_duck_level 0x44
#define GameSound_duck_ms 0x48
#define GameSound_attached 0x4c
#define GameSound_bytes 0x50
#define GameUser_name 0x0
#define GameUser_date_a 0x8
#define GameUser_date_b 0x12
#define GameUser_id 0x1c
#define GameUser_bytes 0x20
#define GameImageFormat_height 0x0
#define GameImageFormat_width 0x4
#define GameImageFormat_row_bytes 0x8
#define GameImageFormat_bits_per_pixel 0x18
#define GameImageFormat_pixel_format 0x1c
#define GameImageFormat_bytes 0x20
#define GameSetting_id 0x0
#define GameSetting_name 0x4
#define GameSetting_name_bytes 0x8
#define GameSetting_read 0xc
#define GameSetting_argument 0x10
#define GameSetting_bytes 0x14
#define GameUserList_entries 0x0
#define GameUserList_count 0x4
#define GameUserList_next_id 0x18
#define GameUserList_dirty 0x1a
#define GameFileService_storage 0x8
#define GameFileService_handles 0xc
#define GameFileService_lists 0x10
#define GameFileService_lock 0x14
#define GameFileService_queue_head 0x28
#define GameFileService_queue_tail 0x2c
#define GameFileQueueEntry_request 0x0
#define GameFileQueueEntry_path 0x4
#define GameFileQueueEntry_volume 0xc
#define GameFileQueueEntry_read_only 0xd
#define GameFileQueueEntry_create 0xe
#define GameFileQueueEntry_name 0x10
#define GameFileQueueEntry_next 0x18
#define GameFileQueueEntry_bytes 0x20
#define GameSaveTable_slots 0x0
#define GameSaveTable_total_bytes 0x90
#define GameBacklight_level 0x0
#define GameBacklight_dimmed 0x4
#define GameBacklight_set 0x5
#define EGLNativeWindow_mode 0x0
#define EGLNativeWindow_width 0x4
#define EGLNativeWindow_height 0x6
#define EGLNativeWindow_bytes 0x8
#define BitmapCache_service 0x0
#define BitmapCache_resource_id 0x4
#define BitmapCache_loaded 0x8
#define BitmapCache_bitmap 0xc
#define BitmapCache_context 0xc4
#define BitmapCache_bytes 0xc8
#define GraphicsContext_bytes 0x44
#define GraphicsContext_mode 0x10
#define GraphicsContext_color 0x11
#define GraphicsContext_background_color 0x15
#define GraphicsContext_bitmap 0x1c
#define GraphicsContext_font 0x20
#define GraphicsContext_font_flags 0x28
#define GraphicsContext_origin_x 0x2c
#define GraphicsContext_origin_y 0x30
#define GraphicsContext_clip_top 0x34
#define GraphicsContext_clip_left 0x38
#define GraphicsContext_clip_bottom 0x3c
#define GraphicsContext_clip_right 0x40
#define BMap_format 0x0
#define BMap_stride 0x4
#define BMap_depth 0x6
#define BMap_bounds 0x8
#define BMap_payload_bytes 0x18
#define BMap_payload 0x1c
#define FontResource_family 0x0
#define FontResource_size 0x4
#define FontResource_style 0x8
#define FontResource_bytes 0xc
#define Bitmap_pixels 0x4
#define Bitmap_stride 0x8
#define Bitmap_bits_per_pixel 0xc
#define Bitmap_bytes_per_pixel 0x10
#define Bitmap_format 0x14
#define Bitmap_bounds 0x98
#define Bitmap_owns_pixels 0xb4
#define Bitmap_bytes 0xb8
#define PixelBuffer_pixels 0x0
#define PixelBuffer_width 0x4
#define PixelBuffer_height 0x6
#define PixelBuffer_bytes 0x8
#define Surface_pixels 0x0
#define Surface_stride 0x4
#define Surface_bits_per_pixel 0x8
#define Surface_bytes_per_pixel 0xc
#define Surface_format 0x10
#define Surface_bounds 0x14
#define UIKeyName_code 0x0
#define UIKeyName_name 0x4
#define UIKeyName_bytes 0x8
#define WheelState_notify_pending 0x0
#define WheelState_reported_buttons 0x18
#define WheelState_buttons 0x20
#define WheelState_skip_packet 0x1
#define WheelState_threshold 0x4
#define WheelState_motion 0x8
#define WheelState_position 0xc
#define WheelState_reported_position 0x10
#define WheelState_touch_count 0x14
#define WheelState_previous_touch 0x1c
#define WheelState_touch 0x24
#define InputEvent_kind 0x4
#define InputEvent_wheel_delta 0xc
#define InputEvent_wheel_speed 0x14
#define InputEvent_key 0x10
#define EventManager_events 0x8
#define EventManager_handler_capacity 0xc
#define EventManager_handler_count 0x10
#define EventManager_handlers 0x14
#define EventManager_queue 0x18
#define EventManager_free_events 0x1c
#define EventManager_free_events_tail 0x20
#define EventManager_free_lock 0x24
#define EventManagerEvent_next 0x0
#define EventManagerEvent_message 0x4
#define EventManagerEvent_bytes 0x20
#define WheelController_status 0xc
#define WheelController_acknowledge 0x14
#define WheelController_packet 0x18
#define ITunesReader_file 0x40000
#define ITunesReader_owner 0x40004
#define MediaBrowser_preferences 0xf00
#define MediaBrowser_library 0xf64
#define MediaBrowser_track_data 0xf60
#define MediaBrowser_artists 0xe64
#define MediaBrowser_albums 0xeb0
#define MediaBrowser_albums_end 0xeb4
#define MediaBrowser_artist_tokens 0xe4c
#define MediaBrowser_artist_jump 0xe50
#define MediaBrowser_artist_count 0xe54
#define MediaBrowser_order_flag 0xe3c
#define MediaBrowser_order_selector 0xe38
#define LibraryBrowser_browser 0x40
#define AlbumGroup_token 0x0
#define AlbumGroup_artist 0x4
#define AlbumGroup_display_artist 0x8
#define AlbumGroup_artwork 0xc
#define AlbumGroup_tracks 0x10
#define AlbumGroup_flags 0x14
#define AlbumGroup_bytes 0x18
#define MediaEntry_list 0x0
#define MediaEntry_parent 0x4
#define MediaEntry_next 0x8
#define MediaEntry_previous 0xc
#define MediaEntry_id 0x10
#define MediaEntry_flags 0x1c
#define MediaEntry_song_record 0x20
#define MediaEntry_song_shuffle_rank 0x24
#define MediaEntry_song_next_record_entry 0x28
#define MediaEntry_group_first 0x20
#define MediaEntry_group_last 0x24
#define MediaEntry_groups 0x2c
#define MediaEntry_tracks 0x2e
#define MediaEntry_size 0x30
#define MediaList_track_data 0xc
#define MediaList_root 0x40
#define MediaList_order_mode 0xc8
#define MediaList_repeat_mode 0x184
#define MediaList_flags 0x18c
#define MediaList_order_flags 0x18d
#define MediaList_shuffle_index 0x3b4
#define MediaList_order_index 0x3ac
#define MediaList_jump_index 0x544
#define MediaListSelection_artwork_category 0x83f
#define ArtworkSession_window 0x38
#define ArtworkSession_listener 0x20
#define ArtworkSession_bytes 0x8ac
#define ArtworkSessionListener_cover_loaded_method 0x8
#define MediaOrder_capacity 0x4
#define MediaOrder_bytes 0x8
#define MediaOrder_count 0xc
#define MediaOrder_entries 0x10
#define MediaItem_path_method 0x10
#define MediaItem_bookmark_method 0x90
#define MediaItem_start_time_method 0x94
#define MediaItem_stop_time_method 0x98
#define MediaItem_remembers_position_method 0xe8
#define MediaItem_entry 0x4
#define MediaItem_record 0x8
#define MediaItem_browser 0xc
#define MediaItem_generation 0x10
#define MediaItem_flags 0x14
#define MediaItem_bytes 0x18
#define MediaItem_title_method 0x50
#define MediaItem_album_method 0x54
#define MediaItem_artist_method 0x58
#define MediaItem_display_artist_method 0x5c
#define MediaItem_genre_method 0x60
#define MediaItem_composer_method 0x64
#define MediaItem_grouping_method 0x68
#define MediaItem_duration_method 0xa0
#define MediaItem_file_size_method 0xac
#define MediaItem_bitrate_method 0xb0
#define MediaItem_year_method 0xb4
#define MediaItem_rating_method 0xb8
#define MediaItem_track_number_method 0xbc
#define MediaItem_disc_number_method 0xc0
#define MediaItem_play_count_method 0xc4
#define MediaItem_skip_count_method 0xcc
#define MediaItem_chapter_count_method 0x138
#define MediaItem_chapter_start_method 0x13c
#define MediaItem_chapter_title_method 0x140
#define MediaItem_chapter_index_method 0x144
#define MediaItem_eq_settings_id_method 0xd4
#define MediaItem_file_type_method 0x6c
#define MediaItem_comment_method 0x74
#define MediaItem_category_method 0x78
#define MediaItem_description_method 0x7c
#define MediaItem_show_name_method 0x80
#define MediaItem_date_added_method 0xa4
#define MediaItem_date_modified_method 0xa8
#define MediaItem_last_played_method 0xc8
#define MediaItem_last_skipped_method 0xd0
#define MediaItem_release_date_method 0xdc
#define DateTime_second 0x0
#define DateTime_minute 0x1
#define DateTime_hour 0x2
#define DateTime_day 0x3
#define DateTime_month 0x4
#define DateTime_year 0x6
#define DateTime_weekday 0x8
#define DateTime_bytes 0xa
#define MediaListView_list 0x4
#define MediaListView_browser 0x8
#define MediaListView_generation 0xc
#define MediaRecord_entries 0x24
#define MediaRecord_eq_settings_id 0x54
#define MediaRecord_date_added 0x58
#define MediaRecord_date_modified 0x6c
#define MediaRecord_sample_rate 0x74
#define MediaRecord_volume 0x7c
#define MediaRecord_sound_check 0x82
#define MediaRecord_track_number 0x84
#define MediaRecord_track_total 0x86
#define MediaRecord_disc_number 0x88
#define MediaRecord_disc_total 0x8a
#define MediaRecord_last_played 0x98
#define MediaRecord_last_skipped 0xa8
#define MediaRecord_media_kind 0xbc
#define MediaPlayer_current_track 0x54
#define MediaPlayer_prepared_track 0x58
#define MediaPlayer_playback_view 0x60
#define MediaPlayer_source_view 0x74
#define MediaPlayer_artwork_session 0x88
#define MediaPlayer_playback_source 0x948
#define MediaPlayer_play_index_method 0xe0
#define MediaPlayer_remove_entry_method 0x130
#define MediaPlayer_current_index_method 0x158
#define MediaPlayer_current_item_method 0x15c
#define MediaPlayer_item_at_index_method 0x170
#define MediaPlayer_stop_method 0xec
#define MediaPlayer_apply_eq_settings_id_method 0xa0
#define MediaPlayer_eq_settings_id_method 0xa4
#define MediaPlayer_eq_menu_index_method 0xb4
#define MediaPlayer_eq_settings_id 0x94d
#define MediaPlayer_seek_method 0x100
#define MediaPlayer_current_index 0x5c
#define MediaPlayer_mutex 0xa4c
#define NowPlayingModel_current_item 0x1c
#define SongModel_get_item_method 0x22c
#define TrackData_artist_cache 0x118
#define TrackData_artwork_id 0xf0
#define TrackData_dbid 0x110
#define MediaListModel_source 0x18
#define MediaListModel_all_row 0x25
#define MediaListModel_status_icon 0x258
#define MediaListModel_selected_status_icon 0x310
#define BlockCache_free_blocks 0x4
#define BlockCache_free_count 0x14
#define BlockCache_clients 0x28
#define BlockCache_block_bytes 0x30
#define BlockCache_total_blocks 0x34
#define BlockCache_trimmed 0x38
#define BlockCache_task_queue 0x3c
#define BlockCache_lock 0x148
#define BlockCache_core_mutex 0x180
#define BlockCache_pending_cond 0x19c
#define BlockCache_assigned_cache 0x1bc
#define BlockCache_pending_count 0x1c0
#define BlockCacheClient_cache 0x4
#define BlockCacheClient_blocks 0x8
#define BlockCacheClient_block_count 0x18
#define BlockCacheClient_assigned_blocks 0x1c
#define BlockCacheClient_mutex 0x24
#define BlockCacheClient_weight 0x40
#define BlockCacheClient_flags 0x44
#define BlockCacheClient_desired_bytes 0x48
#define EAppBlockManager_client 0x14
#define EAppBlockManager_block_list 0x28
#define EAppBlockManager_block_count 0x2c
#define EAppBlockManager_request_blocks 0x34
#define FixedPool_magic 0x0
#define FixedPool_slot_size 0x4
#define FixedPool_slots 0x8
#define FixedPool_chunks 0xc
#define FixedPoolChunk_magic 0x0
#define FixedPoolChunk_pool 0x4
#define FixedPoolChunk_next 0x8
#define FixedPoolChunk_slots 0xc
#define FixedPoolChunk_free 0x10
#define FixedPoolChunk_first 0x14
#define FixedPoolChunk_last 0x18
#define FixedPoolChunk_free_list 0x1c
#define FixedPoolChunk_bytes 0x20
#define MainHeap_free_bytes 0x0
#define MainHeap_total_bytes 0x4
#define MainHeap_used_bytes 0x8
#define MediaStream_seek_method 0x24
#define MediaStream_parser 0x107c
#define MediaStream_duration_ms 0x108c
#define MediaParser_seek_to_time_method 0x10
#define MediaParser_read_samples_method 0x14
#define MP4Parser_file 0x8
#define MP4Parser_duration_ms 0x58
#define MP4Parser_movie_timescale 0x64
#define MP4Parser_audio_track 0x1b8
#define MP4Parser_audio_fourcc 0x1c0
#define MP4Track_sample_sizes 0xc
#define MP4Track_chunk_offsets 0x10
#define MP4Track_sample_to_chunk 0x14
#define MP4Track_time_to_sample 0x18
#define MP4Track_edits 0x1c
#define MP4Track_chunk_offset_atom 0x48
#define MP4Track_sample_size_atom 0x58
#define MP4Track_sample_to_chunk_atom 0x60
#define MP4Track_sample 0x68
#define MP4Track_sample_to_chunk_index 0x6c
#define MP4Track_chunk 0x70
#define MP4Track_sample_in_chunk 0x74
#define MP4Track_chunk_byte_offset 0x78
#define MP4Track_timescale 0x7c
#define MP4Track_time_to_sample_run 0x84
#define MP4Track_presentation_time 0x88
#define MP4Track_decode_time 0x90
#define MP4Track_duration 0x98
#define MP4Track_time_to_sample_index 0xa0
#define MP4Track_edit_index 0xa4
#define MP4Track_edit_media_start 0xa8
#define MP4Track_edit_media_end 0xb0
#define MP4Track_edit_duration 0xb8
#define MP4Track_bytes 0xd8
#define MP4SampleSizeTable_constant_size 0x0
#define MP4SampleSizeTable_count 0x4
#define MP4SampleSizeTable_window_first 0x8
#define MP4SampleSizeTable_window 0xc
#define MP4SampleTable_count 0x0
#define MP4SampleTable_window_first 0x4
#define MP4SampleTable_window 0x8
#define MP4SampleToChunkEntry_first_chunk 0x0
#define MP4SampleToChunkEntry_samples_per_chunk 0x4
#define MP4SampleToChunkEntry_description 0x8
#define MP4SampleToChunkEntry_bytes 0xc
#define MP4TimeToSampleEntry_count 0x0
#define MP4TimeToSampleEntry_delta 0x4
#define MP4TimeToSampleEntry_bytes 0x8
#define MP4EditList_count 0x0
#define MP4EditList_entries 0x4
#define App_browser 0x30
#define App_current_item 0x38
#define App_current_index 0x50
#define App_sleep_timer_remaining 0x49c
#define App_disk_mode_requested 0x4cc
#define App_library_load_started 0x4d2
#define App_in_disk_mode 0x4d3
#define PowerController_backlight_timer 0x6c
#define PowerController_idle_timer 0x98
#define PowerController_game_running 0xd7
#define PowerController_claims_wheel_events 0xcc
#define PowerController_skips_wheel_decode 0xcd
#define PowerController_sleep_after_wake 0xc4
#define StorageArbiter_active 0x10
#define StorageArbiter_clients 0x14
#define StorageArbiter_owner 0x20
#define StorageArbiter_pending 0x24
#define ScsiDiskClient_context 0x8
#define ScsiDiskClient_wanted 0xc
#define ScsiDiskClient_state 0xd
#define I2C0Controller_control 0x0
#define I2C0Controller_status 0x4
#define I2C0Controller_data 0xc
#define I2C0Controller_busy 0x10
#define I2C0Controller_transfer_status 0x20
#define ResourceBlock_length 0x0
#define ResourceBlock_kind 0x4
#define ResourceBlock_data 0x8
#define ResourceBlockList_type 0x8
#define ResourceBlockList_id 0xc
#define ResourceBank_kind 0x0
#define ResourceBank_rom_index 0x4
#define ResourceBank_overrides 0x18
#define ResourceOverride_length 0x0
#define ResourceOverride_data 0x4
#define ResourceOverride_bytes 0x8
#define ResourceRomHeader_version 0x0
#define ResourceRomHeader_index_bytes 0x4
#define ResourceRomHeader_type_count 0x8
#define ResourceRomHeader_types 0xc
#define ResourceRomHeader_bytes 0xc
#define ResourceRomType_type 0x0
#define ResourceRomType_count 0x4
#define ResourceRomType_entries 0xc
#define ResourceRomType_bytes 0x10
#define ResourceRomEntry_id 0x0
#define ResourceRomEntry_offset 0x4
#define ResourceRomEntry_length 0x8
#define ResourceRomEntry_bytes 0xc
#define RomResourceIndex_index 0x0
#define RomResourceIndex_data 0x8
#define RomResourceIndex_type_count 0xc
#define RomResourceIndex_types 0x10
#define Preferences_object 0x378
#define Preferences_get_method 0xdc
#define Preferences_set_method 0xd4
#define Preferences_get_typed_method 0xe0
#define Preferences_set_typed_method 0xd8
#define PreferencesFile_clicker_route 0xb00
#define PreferencesFile_eq_settings_id 0xb64
#define PreferencesFile_time_zone_id 0xb74
#define PreferencesFile_dirty 0xb94
#define TimeZone_city_name 0x4
#define TimeZone_region 0x8
#define TimeZone_zone_id 0xa
#define TimeZone_map_x 0xc
#define TimeZone_map_y 0xe
#define TimeZone_offset_key 0x10
#define TimeZone_bytes 0x14
#define TimeZoneOffset_minutes 0x0
#define TimeZoneOffset_key 0x2
#define TimeZoneOffset_bytes 0x4
#define DirectoryIterator_bytes 0x44
#define NativePath_bytes 0x8
#define LegacyPath_volume 0x0
#define LegacyPath_text 0x2
#define ThreadRecord_kernel_thread 0x0
#define ThreadRecord_context 0x8
#define ThreadRecord_name 0xc
#define ThreadRecord_stack_base 0x40
#define ThreadRecord_bytes 0x44
#define IRQFrame_spsr 0x0
#define IRQFrame_r0 0x4
#define IRQFrame_lr 0x38
#define IRQFrame_pc 0x3c
#define IRQFrame_bytes 0x40
#define FileHandle_filesystem 0x4
#define FileHandle_writable 0x9
#define FileHandle_path 0xc
#define FileHandle_read_only 0x14
#define FileHandle_descriptor 0x18
#define FileHandle_status 0x1c
#define FileHandle_size 0x20
#define FileHandle_position 0x34
#define FileHandle_buffered 0x38
#define MenuSettingsRow_item 0x0
#define MenuSettingsRow_title 0x4
#define MenuSettingsRow_layout_kind 0x8
#define MenuSettingsRow_preference 0xc
#define MenuSettingsRow_visible 0x10
#define MenuSettingsRow_bytes 0x14
#define CppString_bytes 0x4
#define NativeString_data 0x4
#define NativeString_bytes 0x8
#define StringInputStream_bytes 0x7c
#define Model_notify_method 0x58
#define ScreenController_push_method 0xd0
#define ScreenController_below 0x14
#define ScreenController_selection_model 0x44
#define ScreenController_pop_method 0xbc
#define ScreenController_switch_layout_method 0x120
#define ScreenController_emit_event_method 0x11c
#define ScreenController_pop_self_method 0xb8
#define ScreenController_named_event_method 0x110
#define ScreenController_show_layout_method 0x124
#define ScreenController_action_method 0x114
#define ScreenController_vtable_bytes 0x128
#define ScreenController_screen 0xb4
#define ScreenController_settings_model 0xb0
#define ScreenController_event_method 0x30
#define ScreenController_activate_method 0xec
#define ScreenController_deactivate_method 0xf0
#define Service_interface_method 0x14
#define AlarmsService_sleep_timer_choice 0x48
#define AlarmsService_sleep_timer_choices 0x90
#define AlarmsService_sleep_timer_choice_count 0x94
#define SleepTimerChoice_label 0x0
#define SleepTimerChoice_minutes 0x4
#define SleepTimerChoice_bytes 0x8
#define ProgressBar_value_method 0x114
#define ProgressBar_set_value_method 0x118
#define ProgressBar_set_minimum_method 0x11c
#define ProgressBar_set_maximum_method 0x120
#define ProgressBar_load_bitmap_caches_method 0x134
#define ProgressBar_draw_markers_method 0x138
#define ProgressBar_value 0xa8
#define ProgressBar_minimum 0xac
#define ProgressBar_maximum 0xb0
#define ProgressBar_marker_count 0xb4
#define ProgressBar_marker_resource 0xb8
#define ProgressBar_marker_cache 0xbc
#define ProgressBar_thumb_resource 0xc0
#define ProgressBar_thumb_cache 0xc4
#define ProgressBar_empty_left_resource 0xc8
#define ProgressBar_empty_left_cache 0xcc
#define ProgressBar_empty_right_resource 0xd0
#define ProgressBar_empty_right_cache 0xd4
#define ProgressBar_empty_middle_resource 0xd8
#define ProgressBar_empty_middle_cache 0xdc
#define ProgressBar_fill_left_resource 0xe0
#define ProgressBar_fill_left_cache 0xe4
#define ProgressBar_fill_right_resource 0xe8
#define ProgressBar_fill_right_cache 0xec
#define ProgressBar_fill_middle_resource 0xf0
#define ProgressBar_fill_middle_cache 0xf4
#define ProgressBar_alternate_left_resource 0xf8
#define ProgressBar_alternate_left_cache 0xfc
#define ProgressBar_alternate_right_resource 0x100
#define ProgressBar_alternate_right_cache 0x104
#define ProgressBar_alternate_middle_resource 0x108
#define ProgressBar_alternate_middle_cache 0x10c
#define ProgressBar_style 0x110
#define ProgressBar_timer 0x114
#define ProgressBar_flags 0x140
#define ProgressBar_descriptor_byte 0x148
#define ProgressBar_bytes 0x14c
#define ProgressBar_vtable_bytes 0x13c
#define View_parent 0x34
#define View_model 0x38
#define View_property 0x44
#define View_refresh_method 0xd4
#define View_destroy_method 0x4
#define View_bytes 0xa4
#define View_options_bytes 0x5c
#define View_vtable_bytes 0x114
#define View_draw_method 0x74
#define View_draw_clipped_method 0x70
#define View_background_color 0x90
#define View_window 0x3c
#define View_bounds 0x80
#define View_id 0x4c
#define View_find_view_method 0xe4
#define View_linked_view_method 0xfc
#define View_has_event_target_method 0x18
#define View_flags 0x48
#define View_show_method 0xa0
#define View_parent_shown_method 0xa4
#define View_enable_method 0xc0
#define View_parent_enabled_method 0xc4
#define View_children 0xa8
#define View_child_count 0xac
#define View_child_items 0xb0
#define Window_bounds 0x20
#define Window_bitmap 0x54
#define Window_parent 0x78
#define Window_dirty_rect 0xdc
#define BitmapView_context 0x15c
#define BitmapView_color 0x160
#define KeyboardView_letter_color 0x1cd
#define KeyboardView_selected_letter_color 0x1d1
#define OptionBarView_segment_count 0xa8
#define OptionBarView_style 0x121
#define OptionBarView_selected 0x124
#define OptionBarView_label_color 0x1cc
#define OptionBarView_selected_label_color 0x1d0
#define OptionBarView_MaxSegments 0x3
#define CalendarView_header_color 0x104
#define CalendarDayCell_day_color 0xf4
#define CalendarDayCell_art 0x1b4
#define CalendarDayCell_top_marker 0x274
#define CalendarDayCell_bottom_marker 0x334
#define GLAlbumArtView_animation 0xac
#define GLAlbumArtAnimation_renderer 0x128
#define EventTimer_period_ms 0x4
#define EventTimer_recipient 0x24
#define EventTimer_event_queue 0x28
#define EventTimer_bytes 0x2c
#define Event_kind 0x4
#define Event_notification 0x8
#define Event_payload_method 0x14
#define TimerEventPayload_timer 0x8
#define EventRecipient_event_method 0x30
#define MenuRow_id 0x30
#define MenuRow_style 0x60
#define MenuRow_visible 0x64
#define MenuRow_title 0x68
#define MenuRow_title_from_model 0x94
#define Layout_screen_list_record 0x4
#define Layout_id 0x8
#define Layout_instances 0x10
#define Layout_instances_end 0x14
#define Layout_events 0x38
#define Layout_screen 0x58
#define Layout_kind 0xc
#define Layout_bytes 0x5c
#define LayoutInstance_id 0x8
#define LayoutScreen_id 0x4
#define ResourceScreenController_screen_definition 0xb0
#define ResourceScreenController_bytes 0xec
#define ExtrasMenuController_screen_definition 0xb8
#define ExtrasMenuController_bytes 0xf4
#define MainMediaMenuController_screen_definition 0xc8
#define MainMediaMenuController_bytes 0x104
#define SettingsMenuListController_screen_definition 0xc0
#define SettingsMenuListController_bytes 0xfc
#define ScreenDefinition_layout 0x30
#define ScreenDefinition_transition 0x36
#define ScreenDefinition_screen 0x4
#define ScreenDefinition_layout_cache 0xc
#define SsinInstance_view_bindings 0x0
#define SsinInstance_template 0x4
#define SsinInstance_layout_events_id 0x8
#define SsinInstance_flags 0x10
#define SsinInstance_enter_style 0xc
#define SsinInstance_leave_style 0xe
#define SsinInstance_shared_style 0x10
#define ViewDescriptor_frame 0x0
#define ViewDescriptor_left_offset 0x0
#define ViewDescriptor_left_reference_view 0x4
#define ViewDescriptor_left_anchor_mode 0x8
#define ViewDescriptor_top_offset 0xc
#define ViewDescriptor_top_reference_view 0x10
#define ViewDescriptor_top_anchor_mode 0x14
#define ViewDescriptor_bottom_offset 0x18
#define ViewDescriptor_bottom_reference_view 0x1c
#define ViewDescriptor_bottom_anchor_mode 0x20
#define ViewDescriptor_right_offset 0x24
#define ViewDescriptor_right_reference_view 0x28
#define ViewDescriptor_right_anchor_mode 0x2c
#define ViewDescriptor_id 0x30
#define ViewDescriptor_type 0x4c
#define ViewDescriptor_options_bytes 0x5c
#define TextViewDescriptor_source 0x64
#define TextViewDescriptor_font 0x68
#define TextViewDescriptor_color 0x70
#define ImageViewDescriptor_source 0x74
#define AnalogClockDescriptor_source 0x5c
#define AnalogClockDescriptor_style 0x60
#define ProgressDescriptor_maximum 0x5c
#define ProgressDescriptor_minimum 0x60
#define ProgressDescriptor_source 0x64
#define ProgressDescriptor_style 0x6c
#define WheelAccumulator_last_event_time 0x0
#define WheelAccumulator_acceleration_deadline 0x4
#define WheelAccumulator_gesture_deadline 0x8
#define WheelAccumulator_multiplier 0xc
#define WheelAccumulator_last_motion 0x10
#define WheelAccumulator_remainder 0x14
#define WheelAccumulator_threshold 0x18
#define WheelAccumulator_max_multiplier 0x1c
#define WheelAccumulator_last_result 0x20
#define WheelAccumulator_accelerate 0x24
#define WheelAccumulator_multiplier_disabled 0x25
#define WheelAccumulator_fast_speed 0x28
#define WheelAccumulator_slow_speed 0x2c
#define WheelAccumulator_interval 0x30
#define WheelAccumulator_bytes 0x34
#define ItemTableBinding_item_table 0x4
#define ViewLayout_title 0x24
#define SourceBinding_service 0x0
#define SourceBinding_property 0x4
#define SourceBinding_destination 0x8
#define SelectionModel_current_index_method 0x168
#define SelectionModel_find_view_method 0xf8
#define ListInterface_select_method 0x174
#define Notification_code 0x4
#define ControlRange_value 0x0
#define ControlRange_minimum 0x4
#define ControlRange_maximum 0x8
#define ControlRange_bytes 0xc
#define MenuList_item_blocks 0x18
#define LayoutManager_screen 0x20
#define LayoutManager_template_owner 0x38
#define LayoutManager_layout 0x28
#define LayoutManager_kind 0x2c
#define LayoutManager_saved_transition 0x80
#define ViewOptions_left_offset 0x0
#define ViewOptions_top_offset 0xc
#define ViewOptions_bottom_offset 0x18
#define ViewOptions_right_offset 0x24
#define ViewOptions_bytes 0x5c
#define Rect_top 0x0
#define Rect_left 0x4
#define Rect_bottom 0x8
#define Rect_right 0xc
#define Rect_bytes 0x10
#define TemplateView_enter_styles 0xfc
#define TemplateView_leave_styles 0x107
#define TemplateView_shared_styles 0x112
#define TemplateView_kind 0x11e
#define TemplateOwner_context 0x38
#define TemplateOwner_bounds 0x80
#define TemplateOwner_templates 0xa8
#define TemplateOwner_list_kinds 0x108
#define TemplateResource_has_styles 0x4
#define TemplateResource_styles 0x6
#define TemplateStyle_enter 0x0
#define TemplateStyle_leave 0x2
#define TemplateStyle_shared 0x8
#define TemplateStyle_bytes 0xc
#define ContextMenuModel_items 0x18
#define ContextMenuModel_remembered_item 0x30
#define ContextMenuItems_bytes 0x18
#define ContextMenuItem_item 0x0
#define ContextMenuItem_action 0x4
#define ContextMenuItem_flags 0x8
#define ContextMenuItem_bytes 0xc
#define MediaListController_show_context_menu_method 0x148
#define MediaListController_map_visible_row_method 0x130
#define MediaListController_model 0xb4
#define SongListController_context_row 0xd4
#define SongListController_layout_cache 0xd8
#define SongListController_layout 0x108
#define GeniusController_context_row 0xd4
#define GeniusController_layout_cache 0xd8
#define GeniusController_layout 0x108
#define NestedPlaylistsController_context_row 0xe8
#define NestedPlaylistsController_layout_cache 0xf0
#define NestedPlaylistsController_layout 0x120
#define ArtistsController_layout_cache 0xd4
#define ArtistsController_layout 0x104
#define AlbumsController_layout_cache 0xd8
#define AlbumsController_layout 0x108
#define MainMediaMenu_preview_kind 0xb0
#define NowPlayingController_layout_cache 0xec
#define NowPlayingController_layout 0x11c
#define Navigator_top_controller 0x4
#define TextView_context 0xa4
#define TextView_text 0xf8
#define ScreenLayoutEntry_layout 0x0
#define ScreenLayoutEntry_kind 0x4
#define ScreenLayoutEntry_refresh 0x8
#define ScreenLayoutEntry_bytes 0xc
#define PodcastEpisodesController_layout_cache 0xd0
#define PodcastEpisodesController_layout 0x100
#define ITunesUEpisodesController_layout_cache 0xd0
#define ITunesUEpisodesController_layout 0x100
#define AudiobooksController_layout_cache 0xd8
#define AudiobooksController_layout 0x108
#define MoviesController_layout_cache 0xd4
#define MoviesController_layout 0x104
#define TVEpisodesController_layout_cache 0xd0
#define TVEpisodesController_layout 0x100
#define NestedVideoPlaylistsController_context_row 0xe8
#define NestedVideoPlaylistsController_layout_cache 0xf4
#define NestedVideoPlaylistsController_layout 0x124
#define USBDevice_controller 0x34
#define USBDevice_interfaces 0x38
#define USBDevice_configuration_set 0x5c
#define USBDevice_setup 0x90
#define USBDevice_request_type 0x90
#define USBDevice_request 0x91
#define USBDevice_value 0x92
#define USBDevice_index 0x94
#define USBDevice_length 0x96
#define USBDevice_max_packet 0xa0
#define USBDevice_ep0_buffer 0xb4
#define USBDevice_host_to_device 0xb8
#define USBDevice_data_length 0xbc
#define USBDevice_configuration 0xc4
#define USBDevice_bytes 0xd0

#define ArtworkFormatId_1009 0x3f1
#define ArtworkFormatId_1013 0x3f5
#define ArtworkFormatId_1015 0x3f7
#define ArtworkFormatId_1016 0x3f8
#define ArtworkFormatId_1017 0x3f9
#define ArtworkFormatId_1020 0x3fc
#define ArtworkFormatId_1021 0x3fd
#define ArtworkFormatId_1023 0x3ff
#define ArtworkFormatId_1024 0x400
#define ArtworkFormatId_1027 0x403
#define ArtworkFormatId_1028 0x404
#define ArtworkFormatId_1029 0x405
#define ArtworkFormatId_1031 0x407
#define ArtworkFormatId_1032 0x408
#define ArtworkFormatId_1035 0x40b
#define ArtworkFormatId_1036 0x40c
#define ArtworkFormatId_1050 0x41a
#define ArtworkFormatId_1051 0x41b
#define ArtworkFormatId_1052 0x41c
#define ArtworkFormatId_1053 0x41d
#define ArtworkFormatId_1054 0x41e
#define ArtworkFormatId_1055 0x41f
#define ArtworkFormatId_1060 0x424
#define ArtworkFormatId_1061 0x425
#define ArtworkFormatId_1065 0x429
#define ArtworkFormatId_1066 0x42a
#define ArtworkFormatId_1068 0x42c
#define ArtworkFormatId_1069 0x42d
#define AudioProperty_PostGain 0x0
#define AudioProperty_Speed 0x1
#define AudioProperty_EQPreset 0x2
#define AudioProperty_Unused 0x3
#define EQ_PresetCount 0x17
#define EQ_SettingsId_Off 0x0
#define EQ_SettingsId_First 0x64
#define EQ_SettingsId_Flat 0x6b
#define EQ_SettingsId_Last 0x79
#define DISPLAY_PIPE_BASE 0x38900000
#define DisplayGamma_Entries 0x100
#define DisplayLayerEnable_Layer0 0x40
#define DisplayLayerEnable_Layer1 0x20
#define DisplayLayerEnable_Layer2 0x10
#define DisplayLayerEnable_Layer3 0x8
#define DisplayLayerEnable_Layer4 0x4
#define DisplayLayerEnable_Video 0x80
#define DisplayFormat_RGB565 0x3
#define DisplayFormat_ARGB8888 0x7
#define DisplayFormat_YUV420 0x8
#define DisplayVideoMode_YUV420 0x100
#define DRAM_BASE 0x8000000
#define DRAM_END 0xc000000
#define DRAM_UNCACHED_ALIAS 0x80000000
#define IRAM_BASE 0x22000000
#define IRAM0_END 0x22020000
#define BOOT_DESCRIPTOR 0x2203ff00
#define BOOT_SYSINFO_MAGIC 0x53797349
#define MMIO_BASE 0x38000000
#define MMU_TRANSLATION_TABLE 0x2200c000
#define MMU_UNMAPPED_ADDRESS 0x10000000
#define IRAM_END 0x22040000
#define IRQ_STACK_TOP 0x8b3b1b0
#define GameImage_Magic 0x70706165
#define GameImage_MaxVersion 0x10001000
#define GameImage_MaxEntries 0x5
#define GameImage_LoadAddress 0x18000000
#define GameImage_MaxBytes 0x100000
#define GameSurface_Draw 0x3059
#define GameSurface_Read 0x305a
#define GameSurfaceColorMode_RGB565 0x5
#define GameImagePixelFormat_RGB565 0x0
#define GameImagePixelFormat_RGB565ByteSwapped 0x1
#define GameMediaKind_Music 0x1
#define GameMediaKind_Movie 0x2
#define GameMediaKind_Podcast 0x4
#define GameMediaKind_Audiobook 0x8
#define GameMediaKind_MusicVideo 0x10
#define GameMediaKind_TVShow 0x20
#define GameMediaKind_Kind100 0x40
#define GameMediaKind_Rental 0x80
#define GameMediaKind_ITunesU 0x200000
#define GameButton_Menu 0x1
#define GameButton_Center 0x2
#define GameButton_Next 0x3
#define GameButton_Previous 0x4
#define GameButton_Play 0x5
#define GameButtonEvent_Release 0x1
#define GameButtonEvent_Press 0x2
#define GameFrameState_Running 0x0
#define GameFrameState_ShuttingDown 0x4
#define GameFrameState_ShutDown 0x5
#define GameFrameState_Done 0x6
#define GameStorage_Disk 0x0
#define GameStorage_BuiltIn 0x1
#define GameDirectory_Package 0x0
#define GameDirectory_UserData 0x1
#define GameDirectory_Stats 0x2
#define GameDirectory_SharedData 0x3
#define GameAPI_ModuleRegistryBytes 0xa84
#define GameAPIModule_NameBytes 0x20
#define GameAPIModule_Tail 0x13061973
#define GameImport_Tail 0x29061968
#define GameAPI_MiscTBD_Count 0xd
#define GameAPI_MiscTBD_Id0 0x155ee219
#define GameAPI_MiscTBD_Id1 0x93d0dcbc
#define GameAPI_MiscTBD_Id2 0xd74289cf
#define GameAPI_MiscTBD_Id3 0x221c280b
#define GameAPI_DebugUtil_Count 0x5
#define GameAPI_DebugUtil_Id0 0xeec53825
#define GameAPI_DebugUtil_Id1 0x1fc24a39
#define GameAPI_DebugUtil_Id2 0x5c05a410
#define GameAPI_DebugUtil_Id3 0x5cedfb4f
#define GameAPI_MemoryAlloc_Count 0x3
#define GameAPI_MemoryAlloc_Id0 0xe3d00bdb
#define GameAPI_MemoryAlloc_Id1 0x115445bf
#define GameAPI_MemoryAlloc_Id2 0x9bf892bb
#define GameAPI_MemoryAlloc_Id3 0x9be6a996
#define GameAPI_InputEvents_Count 0x2
#define GameAPI_InputEvents_Id0 0xd05733c7
#define GameAPI_InputEvents_Id1 0xbd747148
#define GameAPI_InputEvents_Id2 0xe578f302
#define GameAPI_InputEvents_Id3 0xe6e13744
#define GameAPI_Settings_Count 0x1
#define GameAPI_Settings_Id0 0x991af58f
#define GameAPI_Settings_Id1 0x1ca24585
#define GameAPI_Settings_Id2 0x70bf1e30
#define GameAPI_Settings_Id3 0x950e59f4
#define GameAPI_AsyncFileIO_Count 0x11
#define GameAPI_AsyncFileIO_Id0 0x60f01200
#define GameAPI_AsyncFileIO_Id1 0xa7c30511
#define GameAPI_AsyncFileIO_Id2 0x4e312551
#define GameAPI_AsyncFileIO_Id3 0x9a98aa17
#define GameAPI_SoundEffect_Count 0x2a
#define GameAPI_SoundEffect_Id0 0xdb7071da
#define GameAPI_SoundEffect_Id1 0x14ce0c2b
#define GameAPI_SoundEffect_Id2 0x43bdda3b
#define GameAPI_SoundEffect_Id3 0x8df78cf7
#define GameAPI_OpenGLES_Count 0xaa
#define GameAPI_OpenGLES_Id0 0x16c6ae7d
#define GameAPI_OpenGLES_Id1 0x1a0305da
#define GameAPI_OpenGLES_Id2 0xc768a66c
#define GameAPI_OpenGLES_Id3 0xb49cfc81
#define GameAPI_Audio_Count 0x36
#define GameAPI_Audio_Id0 0xbf88e153
#define GameAPI_Audio_Id1 0xd5a3310a
#define GameAPI_Audio_Id2 0x30e865ac
#define GameAPI_Audio_Id3 0xbe00bab9
#define GameAPI_Metadata_Count 0xa9
#define GameAPI_Metadata_Id0 0x3b6ee1e
#define GameAPI_Metadata_Id1 0x1b4d1309
#define GameAPI_Metadata_Id2 0xcac3a877
#define GameAPI_Metadata_Id3 0x7bfa9832
#define GameAPI_Users_Count 0xa
#define GameAPI_Users_Id0 0x512e6d21
#define GameAPI_Users_Id1 0x78aae6d4
#define GameAPI_Users_Id2 0xc413a6df
#define GameAPI_Users_Id3 0xaf8cccef
#define GameFileRequest_Open 0x1
#define GameFileRequest_Close 0x2
#define GameFileRequest_Read 0x3
#define GameFileRequest_Write 0x4
#define GameFileRequest_Seek 0x5
#define GameFileRequest_ReadFile 0x6
#define GameFileRequest_WriteFile 0x7
#define GameFileRequest_Delete 0x8
#define GameFileRequest_Rename 0x9
#define GameFileRequest_ListDirectory 0xa
#define GameFileRequest_Complete 0x64
#define GameSave_MaxSlots 0x4
#define GameSave_MaxBytes 0x19000
#define GameSound_VolumeMax 0x7fff
#define GameSound_PanMax 0x7fff
#define GameSound_SpeedMin 0x1f4
#define GameSound_SpeedMax 0x7d0
#define GameSound_SampleRateMin 0x1f40
#define GameSound_SampleRateMax 0xbb80
#define AudioMixer_VoiceCount 0x8
#define GameSoundFlags_QueueTone 0x2
#define GameSoundState_Playing 0x1
#define GameSoundState_Paused 0x2
#define GameSoundState_Stopped 0x3
#define GamePlaybackState_Stopped 0x0
#define GamePlaybackState_Playing 0x1
#define GamePlaybackState_Paused 0x2
#define GameRepeatMode_Off 0x0
#define GameRepeatMode_One 0x1
#define GameRepeatMode_All 0x2
#define GamePlaybackOrder_Off 0x0
#define GamePlaybackOrder_Songs 0x1
#define GamePlaybackOrder_Albums 0x2
#define GameAudio_VolumeMax 0xff
#define GameSettings_Count 0x3
#define GameSettingsStatus_NoMapping 0xffffffd2
#define GameSettingsStatus_NoPreference 0xffffffd1
#define GameSettingsStatus_BufferTooSmall 0xffffffd0
#define GameSettingsStatus_NullBuffer 0xffffffcf
#define GameSettingsStatus_UnknownName 0xffffffce
#define GamesPlatform_IPodVideo 0x1
#define GamesPlatform_Nano3G 0x2
#define GamesPlatform_Classic 0x3
#define GameBacklight_LevelMax 0x64
#define GameBacklight_DimmedLevel 0x14
#define GameDirectoryStatus_More 0x0
#define GameDirectoryStatus_Last 0x1
#define GameSaveStatus_EndOfData 0x5
#define GameSaveStatus_BadOffset 0x6
#define GameSaveStatus_BadSlot 0x8
#define GameSaveStatus_TooLarge 0x19
#define GraphicsDrawMode_Copy 0x0
#define GraphicsDrawMode_Text 0x21
#define GraphicsDrawMode_Blend 0x22
#define BMapFormat_Mask4 0x4
#define BMapFormat_Mask8 0x8
#define BMapFormat_Indexed8 0x64
#define BMapFormat_Indexed16 0x65
#define BMapFormat_RGB565 0x565
#define BMapFormat_BGRA8888 0x1888
#define BMapFormat_ByteSwapped 0x2000
#define WheelController_Ready 0x1
#define WheelController_Acknowledge 0x7
#define WheelPacket_Mask 0xbc0000ff
#define WheelPacket_Value 0x8000001a
#define WheelPacket_ButtonsMask 0x8000ffff
#define WheelPacket_ButtonsValue 0x8000023a
#define WheelButton_Center 0x1
#define WheelButton_Next 0x2
#define WheelButton_Previous 0x4
#define WheelButton_Play 0x8
#define WheelButton_Menu 0x10
#define InputEvent_ButtonDown 0x6
#define InputEvent_ButtonPressAndHold 0x8
#define InputEvent_ButtonLongPressAndHold 0x9
#define InputEvent_ButtonDoubleClick 0xa
#define InputEvent_ButtonDoubleClickAndHold 0xb
#define InputEvent_ButtonTripleClick 0xc
#define InputEvent_ButtonUp 0xd
#define InputEvent_Wheel 0x15
#define UIKey_Play 0x10
#define UIKey_UpArrow 0x57
#define UIKey_DownArrow 0x58
#define UIKey_Previous 0x59
#define UIKey_Next 0x5a
#define UIKey_Menu 0x6e
#define UIKey_Center 0x6f
#define UIKey_RemotePlayPause 0x1c
#define UIKey_RemotePrevious 0x1d
#define UIKey_RemoteNext 0x1e
#define UIKey_RemoteVolumeDown 0x1f
#define UIKey_RemoteVolumeUp 0x20
#define UIKey_RemoteEvent 0x30
#define UIKey_RemoteNextAlbum 0x38
#define UIKey_RemotePreviousAlbum 0x39
#define UIKey_RemoteStop 0x3a
#define UIKey_RemotePlay 0x3b
#define UIKey_RemotePause 0x3c
#define UIKey_RemoteMute 0x3d
#define UIKey_RemoteNextChapter 0x3e
#define UIKey_RemotePreviousChapter 0x3f
#define UIKey_RemoteNextPlaylist 0x40
#define UIKey_RemotePreviousPlaylist 0x41
#define UIKey_RemoteShuffle 0x42
#define UIKey_RemoteRepeat 0x43
#define UIKey_RemoteOn 0x44
#define UIKey_RemoteOff 0x45
#define UIKey_RemoteBacklight 0x46
#define UIKey_RemoteFastForward 0x47
#define UIKey_RemoteRewind 0x48
#define UIKey_RemoteMenu 0x49
#define UIKey_RemoteCenter 0x4a
#define UIKey_RemoteUpArrow 0x4b
#define UIKey_RemoteDownArrow 0x4c
#define UIKey_RemoteBacklightOff 0x4d
#define UIKey_ImagePlayPause 0x1
#define UIKey_ImageNext 0x2
#define UIKey_ImagePrevious 0x3
#define UIKey_ImageStop 0x4
#define UIKey_ImagePlay 0x5
#define UIKey_ImagePause 0x6
#define UIKey_ImageShuffle 0x7
#define UIKey_ImageRepeat 0x8
#define UIKey_VideoPlayPause 0x9
#define UIKey_VideoNext 0xa
#define UIKey_VideoPrevious 0xb
#define UIKey_VideoStop 0xc
#define UIKey_VideoPlay 0xd
#define UIKey_VideoPause 0xe
#define UIKey_VideoFastForward 0xf
#define UIKey_VideoRewind 0x11
#define UIKey_VideoNextChapter 0x12
#define UIKey_VideoPreviousChapter 0x13
#define UIKey_VideoNextFrame 0x14
#define UIKey_VideoPreviousFrame 0x15
#define UIKey_VideoCaption 0x16
#define UIKey_AudioPlayPause 0x17
#define UIKey_AudioVolumeUp 0x18
#define UIKey_AudioVolumeDown 0x19
#define UIKey_AudioNext 0x1a
#define UIKey_AudioPrevious 0x1b
#define UIKey_AudioNextAlbum 0x21
#define UIKey_AudioPreviousAlbum 0x22
#define UIKey_AudioStop 0x23
#define UIKey_AudioPlay 0x24
#define UIKey_AudioPause 0x25
#define UIKey_AudioMute 0x26
#define UIKey_AudioNextChapter 0x27
#define UIKey_AudioPreviousChapter 0x28
#define UIKey_AudioNextPlaylist 0x29
#define UIKey_AudioPreviousPlaylist 0x2a
#define UIKey_AudioShuffle 0x2b
#define UIKey_AudioRepeat 0x2c
#define UIKey_AudioFastForward 0x2d
#define UIKey_AudioRewind 0x2e
#define UIKey_AudioRecord 0x2f
#define UIKey_MikeyCenter 0x79
#define UIKey_MikeyVolumeDown 0x7a
#define UIKey_MikeyVolumeUp 0x7b
#define UIKey_MikeyAll 0x7e
#define WheelPositions 0x60
#define WheelTouch_Down 0x40000000
#define WheelEvent_Signal 0x5
#define EventMessage_Wheel 0x5765656c
#define EventManager_EventCount 0x64
#define AlbumArtist_order_mode 0x2d
#define MediaOrderMode_Database 0x1
#define MediaOrderMode_Shuffle 0x2
#define MediaOrderMode_Playback 0x33
#define MediaListFlags_Shuffled 0x1
#define MediaListFlags_ShuffleAlbums 0x2
#define MediaListFlags_ShuffleOrderBuilt 0x4
#define MediaListFlags_PlaybackList 0x80
#define MediaListOrderFlags_Reversed 0x4
#define MediaListOrderFlags_Restricted 0x40
#define MediaEntryFlags_Group 0x100
#define MediaRepeat_All 0x1
#define MediaRepeat_One 0x2
#define MediaKind_Music 0x1
#define MediaKind_Movie 0x2
#define MediaKind_Podcast 0x4
#define MediaKind_Audiobook 0x8
#define MediaKind_MusicVideo 0x20
#define MediaKind_TVShow 0x40
#define MediaKind_Rental 0x8000
#define MediaKind_ITunesU 0x200000
#define ArtworkFormat_Cover128 0xf
#define MediaStream_SeekEndMarginMs 0x1388
#define MP4_SampleSizeWindow 0x8000
#define MP4_SampleToChunkWindow 0x8000
#define MP4_ChunkOffsetWindow 0x4000
#define MP4_TimeToSampleWindow 0x8000
#define PowerEvent_GameStarted 0x6
#define PowerEvent_Sleep 0xf
#define PowerEvent_SleepAgain 0x12
#define PowerState_IdleSleep 0xb
#define PowerState_RequestedSleep 0xc
#define PMU_I2CWriteAddress 0xe6
#define PMU_BacklightEnableRegister 0x29
#define PMU_BacklightEnabled 0x1
#define PowerGate1_I2C0 0x10
#define I2C_ControlAck 0x80
#define I2C_ControlInterrupts 0x3f00
#define I2C_ControlContinue 0x10
#define I2C_StatusMasterTransmit 0xc0
#define I2C_StatusStart 0x30
#define I2C_StatusBusy 0x20
#define I2C_StatusNack 0x1
#define I2C_ByteDone 0x100
#define I2C_TransferDone 0x2100
#define HardwareSource_DiskModeRequest 0x8
#define HardwareSource_DiskGranted 0x9
#define HardwareSource_HoldChanged 0x6
#define AppMessage_DiskModeRequest 0x63800003
#define AppMessage_DiskGranted 0x63800004
#define AppMessage_HoldOn 0x63800013
#define AppMessage_HoldOff 0x63800014
#define AppMessage_DiskModeExited 0x6380002e
#define ScsiDiskClientState_Refused 0x0
#define ScsiDiskClientState_Pending 0x1
#define ScsiDiskClientState_Granted 0x2
#define Resource_BMap 0x424d6170
#define Resource_Draw 0x44726177
#define Resource_SORC 0x534f5243
#define Resource_ITEM 0x4954454d
#define Resource_TEVT 0x54455654
#define Resource_SEVT 0x53455654
#define Resource_CEVT 0x43455654
#define Resource_SCST 0x53435354
#define Resource_SLst 0x534c7374
#define Resource_SLyt 0x534c7974
#define Resource_SSin 0x5353696e
#define Resource_VCvs 0x56437673
#define Resource_VCrv 0x56437276
#define Resource_VLyt 0x564c7974
#define Resource_View 0x56696577
#define Resource_TMLT 0x544d4c54
#define Resource_String 0x53747220
#define Resource_VSlt 0x56536c74
#define ResourceBankKind_Heap 0x1
#define ResourceBankKind_Rom 0x2
#define ResourceRomImage_bytes 0x431924
#define BMapPayloads_address 0x841b158
#define BMapPayloads_bytes 0x314506
#define FileVolume_Disk 0x0
#define FileVolume_BuiltIn 0x4
#define FileSeek_Start 0x0
#define FileSeek_Current 0x1
#define FileSeek_End 0x2
#define FileError_NotFound 0xffffffd5
#define FileError_EndOfFile 0xffffffd9
#define FileStatus_Ok 0x0
#define FileStatus_VolumeUnavailable 0x2
#define FileStatus_BadHandle 0x4
#define FileStatus_NotFound 0x7
#define FileStatus_OpenForWrite 0x9
#define FileStatus_OpenForRead 0xa
#define FileStatus_ListTruncated 0x14
#define FileStatus_NotPermitted 0x15
#define FileStatus_NoSlot 0x19
#define FileStatus_BadRequestType 0x21
#define ClockRegister_Microseconds 0x3c7000b4
#define Service_Preferences 0x6000
#define Preference_Toggle 0x60bf
#define Preference_Clicker 0x608c
#define Preference_EQMenuIndex 0x6063
#define Preference_Language 0x6066
#define Preference_LanguageFirst 0x6067
#define Preference_TimeFormat12 0x60a9
#define Preference_TimeFormat24 0x60aa
#define DateTime_DefaultYear 0x7d2
#define TimeZone_Count 0xcd
#define TimeZoneOffset_Count 0x20
#define TimeZoneRegion_Africa 0x1
#define TimeZoneRegion_Asia 0x2
#define TimeZoneRegion_Australia 0x3
#define TimeZoneRegion_Atlantic 0x4
#define TimeZoneRegion_Europe 0x5
#define TimeZoneRegion_NorthAmerica 0x6
#define TimeZoneRegion_Pacific 0x7
#define TimeZoneRegion_SouthAmerica 0x8
#define PreferenceType_Uint32 0x55693332
#define Clicker_Both 0x608d
#define Clicker_Off 0x608e
#define Clicker_Headphones 0x608f
#define Clicker_Speaker 0x6090
#define ClickerRoute_Speaker 0x1
#define ClickerRoute_Headphones 0x2
#define ClockRegister_PowerGate0 0x3c500048
#define PowerGate0_ATA 0x20
#define WarmBootTag_address 0x2203ff00
#define WarmBootMode_None 0x0
#define WarmBootMode_DiskMode 0x1
#define WarmBootMode_DiskScan 0x2
#define WarmBootMode_SoftwareUpdate 0x3
#define WarmBootMode_RetailOS 0x4
#define MainMenuSettings_row_count 0x22
#define MusicMenuSettings_row_count 0xd
#define SettingsProperty_ClickerValue 0x8912
#define SettingsProperty_ClickerDescription 0x8914
#define SettingsProperty_ClickerImage 0x8925
#define StringInputStream_ResourceMode 0xc
#define StringInputStream_DestroyComplete 0x2
#define Property_PodcastStatus 0x7835
#define Property_PodcastStatusSelected 0x7836
#define Property_ITunesUStatus 0x783c
#define Property_ITunesUStatusSelected 0x783d
#define AnchorMode_Top 0x1
#define AnchorMode_Left 0x2
#define AnchorMode_Bottom 0x3
#define AnchorMode_Right 0x4
#define Property_String 0x53747220
#define Property_Count 0x564d6178
#define Notify_None 0x4e6f6e65
#define Notify_All 0x2a2a2a2a
#define Service_Settings 0x8900
#define Service_Games 0x6600
#define Service_ScreenLock 0x5780
#define Service_WorldClock 0x6180
#define WorldClockProperty_Time 0x6181
#define WorldClockProperty_Date 0x6185
#define Service_Alarms 0x6200
#define SleepTimerChoice_Count 0x6
#define GamesProperty_CountText 0x6613
#define GamesProperty_SelectedName 0x6612
#define GamesProperty_Name 0x6604
#define GamesProperty_Artwork 0x6610
#define GamesProperty_PlayedText 0x6614
#define Service_NowPlaying 0x7f00
#define Service_MediaArtwork 0x7f80
#define Service_ContextMenu 0x8180
#define ActionSupportQuery 0xdeadbeef
#define Event_Timer 0xf
#define EventPayload_Timer 0xd00
#define Transition_Standard 0x0
#define Transition_ForegroundPush 0x2
#define Transition_ScreenPush 0x3
#define CxxVtable_prefix_bytes 0x8
#define ViewBinding_ItemTable 0x3f1
#define Service_String 0x80
#define SourceDestination_Title 0x1
#define Property_Control 0x436e746c
#define Property_Visible 0x56566973
#define Interface_ResourceService 0x6
#define Interface_List 0x1700
#define Interface_Template 0x3b80
#define Interface_OptionBar 0x9080
#define Event_Notification 0x16
#define Notification_LayoutShown 0x2502
#define Notification_LayoutChanged 0x2503
#define ViewOption_TopEdge 0x3e8
#define ViewOption_LeftEdge 0x3e9
#define ViewOption_BottomEdge 0x3ea
#define ViewOption_RightEdge 0x3eb
#define ViewOption_Visible 0x3ed
#define Property_Minimum 0x564d696e
#define Property_Layout 0x4c4c6179
#define Property_LayoutSet 0x4c4c6180
#define Property_Bitmap 0x424d6170
#define Property_Drawable 0x44726177
#define Property_ItemId 0x4c69746d
#define Property_ItemChanged 0x4c697445
#define MenuRowStyle_ModelLayout 0x1
#define MenuRowStyle_Text 0x2
#define MenuRowStyle_Check 0x3
#define MenuRowStyle_CheckIndented 0x4
#define MenuRowStyle_Submenu 0x5
#define MenuRowStyle_TwoLine 0x6
#define MenuRowStyle_TwoLineSubmenu 0x7
#define MenuRowStyle_Artwork 0xd
#define MenuRowStyle_ArtworkSubmenu 0xe
#define MenuRowStyle_InlineValue 0xc
#define Event_ModelChange 0x13
#define SourceDestination_InlineValue 0xa
#define SourceDestination_RowBitmap 0xc
#define SourceDestination_RowStatusBitmap 0x13
#define SourceDestination_RowStatusSelectedBitmap 0x14
#define ViewType_Progress 0x70676f67
#define ViewType_Text 0x74657874
#define ViewType_Capacity 0x63706374
#define UITask_stack_bytes 0x20000
#define Transition_Instant 0x6
#define Transition_Hidden 0x1
#define LayoutKind_NowPlaying 0x1
#define LayoutKind_CoverFlow 0x2
#define TemplateKind_Standard 0x1
#define TemplateKind_List 0x2
#define LayoutKind_Menu 0x3
#define LayoutKind_SplitList 0x4
#define LayoutKind_List 0x5
#define LayoutKind_MenuPane 0x6
#define LayoutKind_Clock 0x7
#define LibraryGrouping_Artists 0x4
#define LibraryGrouping_Albums 0x5
#define LibrarySort_Artist 0x2
#define LayoutKind_FullScreen 0x8
#define LayoutKind_Page 0x9
#define LayoutKind_System 0xa
#define Interface_Model 0x5
#define ViewFlags_Highlighted 0x8
#define ViewFlags_BackgroundStyle 0x38000
#define ViewType_Image 0x696d6167
#define ViewType_AnalogClock 0x61636c6b
#define Property_DateTime 0x4474546d
#define ViewType_Keyboard 0x6b627264
#define ViewType_OptionBar 0x6f706272
#define ViewType_Calendar 0x63616c76
#define Property_PlayStateWhite 0x7f0c
#define Property_PlayStateBlack 0x7f0d
#define Property_PlayStateTVOut 0x7f0e
#define Property_ShuffleImage 0x7f15
#define Property_RepeatImage 0x7f16
#define SelectionBarColor_TopLine 0x96c5da
#define SelectionBarColor_GradientStart 0x9a4e1
#define SelectionBarColor_GradientEnd 0x63d3
#define SelectionBarColor_BottomLine 0x4386

#define RES_Settings_Off_String 0x0dad0bda
#define RES_Settings_On_String 0x0dad0bdb
#define RES_Settings_Speaker_String 0x0dad0bd3
#define RES_Settings_Headphones_String 0x0dad0bd4
#define RES_Settings_Both_String 0x0dad0bd5
#define RES_Settings_ClickerOff_Description 0x0dad0bd6
#define RES_Settings_ClickerSpeaker_Description 0x0dad0bd7
#define RES_Settings_ClickerHeadphones_Description 0x0dad0bd8
#define RES_Settings_ClickerBoth_Description 0x0dad0bd9
#define RES_Settings_ClickerOn_Image 0x0dad0c03
#define RES_Settings_ClickerOff_Image 0x0dad0c04
#define RES_Settings_Brightness_Image 0x0dad0bff
#define RES_Settings_MainMenu_Item 0x0dad0c91
#define RES_Settings_MusicMenu_Item 0x0dad0c94
#define RES_SettingsMenu_Legal_Item 0x0dad0c7b
#define RES_SettingsMenu_About_Item 0x0dad0c6b
#define RES_SettingsMenu_Shuffle_Item 0x0dad0c6c
#define RES_SettingsMenu_Repeat_Item 0x0dad0c6d
#define RES_SettingsMenu_MainMenu_Item 0x0dad0c6e
#define RES_SettingsMenu_MusicMenu_Item 0x0dad0c6f
#define RES_SettingsMenu_VolumeLimit_Item 0x0dad0c70
#define RES_SettingsMenu_Backlight_Item 0x0dad0c71
#define RES_SettingsMenu_Brightness_Item 0x0dad0c72
#define RES_SettingsMenu_Audiobooks_Item 0x0dad0c73
#define RES_SettingsMenu_RadioRegions_Item 0x0dad0c74
#define RES_SettingsMenu_EQ_Item 0x0dad0c75
#define RES_SettingsMenu_Clicker_Item 0x0dad0c77
#define RES_SettingsMenu_DateTime_Item 0x0dad0c78
#define RES_SettingsMenu_SortBy_Item 0x0dad0c79
#define RES_SettingsMenu_Language_Item 0x0dad0c7a
#define RES_SettingsMenu_ResetSettings_Item 0x0dad0c7c
#define RES_SettingsMenu_Items 0x00000041
#define RES_SettingsMenu_SoundCheck_Item 0x0dad0c76
#define RES_SettingsMenu_Main_Screen 0x0dad0c6a
#define RES_SettingsMenu_Main_Layout 0x0dad0c7f
#define RES_Settings_MainMenu_Screen 0x0dad0c90
#define RES_Settings_MainMenu_Layout 0x0dad0c92
#define RES_Settings_MusicMenu_Screen 0x0dad0c93
#define RES_Settings_MusicMenu_Layout 0x0dad0c95
#define RES_Settings_MusicMenu_Items 0x00000043
#define RES_Settings_Legal_Screen 0x0dad0cb3
#define RES_Settings_Legal_Layout 0x0dad0cb4
#define RES_SettingsMenus_EUVolume_Confirmation_Screen 0x0dad0cac
#define RES_SettingsMenus_EUVolume_Confirmation_Screen_Default 0x0dad0cad
#define RES_SettingsMenus_DialogNotice_EULimitConfirmation_Layout 0x0dad0c46
#define RES_SettingsMenus_DialogNotice_Template 0x0dad02a3
#define RES_DialogNotice_View 0x0dad0c43
#define RES_DialogNotice_SettingsOptions 0x0dad0c45
#define RES_Settings_EQ_Items 0x00000047
#define RES_Settings_EQ_Screen 0x0dad0cae
#define RES_Settings_EQ_Layout 0x0dad0cb0
#define RES_Settings_EQ_Item 0x0dad0caf
#define RES_Settings_MainMenu_Items 0x00000042
#define RES_SettingsPreview_Template 0x0dad0c2e
#define RES_SettingsPreview_Caption_Text 0x0dad0618
#define RES_SettingsPreview_Background_View 0x0dad0c2f
#define RES_SettingsPreview_Background_Image 0x0dad0bf5
#define RES_Settings_Brightness_Screen 0x0dad0c98
#define RES_Settings_Brightness_Screen_Default 0x0dad0c99
#define RES_Settings_Brightness_Template 0x0dad0c5a
#define RES_Settings_Brightness_Bar 0x0dad0c60
#define RES_Settings_Brightness_Value_Text 0x0dad0c61
#define RES_SettingsMenus_Brightness_Layout 0x0dad0c84
#define RES_SettingsMenus_Legal_Layout 0x0dad0c8e
#define RES_SettingsInfo_Template_Brightness_Layout 0x0dad0c39
#define RES_SettingsInfo_Template_Legal_Layout 0x0dad0c40
#define RES_Settings_Icon_View 0x0dad0c30
#define RES_Settings_InfoDetail_View 0x0dad0c31
#define RES_MediaStatus_VideoSelected_Bitmap 0x0dad0714
#define RES_MediaStatus_Video_Bitmap 0x0dad0715
#define RES_MediaStatus_AudioSelected_Bitmap 0x0dad0716
#define RES_MediaStatus_Audio_Bitmap 0x0dad0717
#define RES_Menu_Checked_Bitmap 0x0dad0c08
#define RES_Font_Bold 0x0dad0000
#define RES_Font_BoldSmall 0x0dad0001
#define RES_Font_RegularSmall 0x0dad0020
#define RES_Menu_CheckedSelected_Bitmap 0x0dad0c09
#define RES_Menu_Playing_Bitmap 0x0dad072c
#define RES_Menu_PlayingSelected_Bitmap 0x0dad072d
#define RES_Menu_Chevron_Bitmap 0x0dad0125
#define RES_System_ScrollbarTop_Image 0x0dad0122
#define RES_System_ScrollbarCenter_Image 0x0dad0123
#define RES_System_ScrollbarBottom_Image 0x0dad0124
#define RES_CoverFlow_Proxy_Image 0x0dad0128
#define RES_StatusBarBlack_Background_Image 0x0dad012c
#define RES_About_Capacity_Template 0x0dad0b4e
#define RES_Capacity_View 0x0dad0b4f
#define RES_Capacity_Free_Segment 0x0dad0aef
#define RES_Capacity_Audio_Segment 0x0dad0af2
#define RES_Capacity_Video_Segment 0x0dad0af5
#define RES_Capacity_Photos_Segment 0x0dad0af8
#define RES_Capacity_Other_Segment 0x0dad0afb
#define RES_Capacity_Audio_Swatch_Bitmap 0x0dad0ae4
#define RES_Capacity_Video_Swatch_Bitmap 0x0dad0ae5
#define RES_Capacity_Photos_Swatch_Bitmap 0x0dad0ae6
#define RES_Capacity_Other_Swatch_Bitmap 0x0dad0ae7
#define RES_SettingsPreview_Capacity_Template 0x0dad07c6
#define RES_SettingsList_Template 0x0dad01b2
#define RES_SettingsList_View 0x0dad01b3
#define RES_CoverFlow_Backside_Border_Color 0x0dad05d8
#define RES_NowPlaying_White_Background_View 0x0dad08c5
#define RES_NowPlaying_Music_Background_Template 0x0dad08c4
#define RES_NowPlaying_Music_Background_Template_Default 0x0dad08d0
#define RES_NowPlaying_White_Repeat_ImageView 0x0dad08c8
#define RES_NowPlaying_White_Shuffle_ImageView 0x0dad08c9
#define RES_NowPlaying_Classic_Shuffle_Image 0x0dad0868
#define RES_NowPlaying_Classic_Repeat_Image 0x0dad0869
#define RES_NowPlaying_Classic_Repeat_Once_Image 0x0dad086a
#define RES_NowPlaying_Idle_Template 0x0dad0925
#define RES_NowPlaying_ChapterArt_BackgroundImage_View 0x0dad092d
#define RES_RentalInfo_White_Background_View 0x0dad09ae
#define RES_Search_Input_Text 0x0dad0ddc
#define RES_Alarms_PickDate_Preivew_NumericDay 0x0dad030e
#define RES_Settings_DateTime_NumericDay 0x0dad0607
#define RES_Settings_DateTime_Preivew_NumericDay 0x0dad061e
#define RES_NowPlaying_StatusBar_Text_View 0x0dad020e
#define RES_LockScreen_TumblerDigit0_View 0x0dad0688
#define RES_LockScreen_TumblerDigit1_View 0x0dad0689
#define RES_LockScreen_TumblerDigit2_View 0x0dad068a
#define RES_LockScreen_TumblerDigit3_View 0x0dad068b
#define RES_VolumeLimitLockScreen_TumblerDigit0_View 0x0dad0698
#define RES_VolumeLimitLockScreen_TumblerDigit1_View 0x0dad0699
#define RES_VolumeLimitLockScreen_TumblerDigit2_View 0x0dad069a
#define RES_VolumeLimitLockScreen_TumblerDigit3_View 0x0dad069b
#define RES_DateTimePicker_Value1 0x0dad0264
#define RES_DateTimePicker_Value2 0x0dad0265
#define RES_DateTimePicker_Value3 0x0dad0266
#define RES_CoverFlow_Backside_AlbumTitle_Text 0x0dad05e6
#define RES_CoverFlow_Backside_AlbumArtist_Text 0x0dad05e7
#define RES_Notes_List_Screen_Alt 0x0dad09fa
#define RES_Notes_List_Screen_Alt_Default 0x0dad09fb
#define RES_Settings_Legal_Template 0x0dad0041
#define RES_StatusBarWhite_Template 0x0dad01a1
#define RES_StatusBarWhite_Text 0x0dad01a6
#define RES_StatusBar_Template 0x0dad020c
#define RES_StatusBar_Title_Text 0x0dad020e
#define RES_FullScreenImage_Template 0x0dad06b4
#define RES_About_Top_Template 0x0dad0b24
#define RES_About_Screen 0x0dad0b6c
#define RES_About_Accessory_Screen 0x0dad0b70
#define RES_Background_Template 0x0dad01d8
#define RES_Stopwatch_Template 0x0dad0d1d
#define RES_Stopwatch_Screen 0x0dad0d46
#define RES_Stopwatch_Screen_Stopped 0x0dad0d47
#define RES_Stopwatch_Menu_Screen 0x0dad0d49
#define RES_Alarms_Sleep_Timer_Screen 0x0dad0322
#define RES_Alarms_Sleep_Timer_Screen_Default 0x0dad0323
#define RES_Games_Menu_Screen 0x0dad065c
#define RES_Games_Menu_Layout 0x0dad065d
#define RES_Game_Screen 0x0dad065e
#define RES_Game_Screen_Layout 0x0dad065f
#define RES_Game_Running_Layout 0x0dad0664
#define RES_GamesMenu_Title_String 0x0dad063d
#define RES_GamesMenu_NoGames_String 0x0dad063e
#define RES_GamesMenu_OneGame_String 0x0dad063f
#define RES_GamesMenu_GameCount_String 0x0dad0640
#define RES_GamesMenu_NeverPlayed_String 0x0dad0641
#define RES_GamesMenu_PlayedOnce_String 0x0dad0642
#define RES_GamesMenu_PlayedTimes_String 0x0dad0643
#define RES_ExtrasMenu_Games_Layout 0x0dad01dc
#define RES_ExtrasPreview_Games_Template 0x0dad019c
#define RES_ExtrasPreview_Games_Image_View 0x0dad019d
#define RES_ExtrasPreview_Games_Caption_Text 0x0dad019e
#define RES_ExtrasPreview_Games_Image 0x0dad019a
#define RES_ExtrasMenu_Alarms_Layout 0x0dad01d7
#define RES_ExtrasPreview_Alarms_Template 0x0dad01da
#define RES_ExtrasPreview_Alarms_Image 0x0dad02fb
#define RES_ExtrasMenu_Contacts_Layout 0x0dad01dd
#define RES_ExtrasPreview_Contacts_Template 0x0dad01de
#define RES_ExtrasPreview_Contacts_Image 0x0dad026c
#define RES_ExtrasMenu_Calendars_Layout 0x0dad01e0
#define RES_ExtrasPreview_Calendars_Template 0x0dad01e1
#define RES_ExtrasPreview_Calendars_Lines_Template 0x0dad01e3
#define RES_ExtrasPreview_Calendars_Image 0x0dad030b
#define RES_ExtrasMenu_Notes_Layout 0x0dad01e5
#define RES_ExtrasPreview_Notes_Template 0x0dad01e6
#define RES_ExtrasPreview_Notes_Image 0x0dad09d7
#define RES_ExtrasMenu_Lock_Layout 0x0dad01e8
#define RES_ExtrasMenu_Clocks_Layout 0x0dad01f0
#define RES_ExtrasPreview_Clocks_Backdrop_Template 0x0dad01f1
#define RES_ExtrasPreview_Clocks_Overlay_Image 0x0dad048a
#define RES_ExtrasPreview_Clocks_Template 0x0dad01f3
#define RES_ExtrasPreview_Clocks_City_Text 0x0dad04d5
#define RES_ExtrasPreview_Clocks_Region_Text 0x0dad04d6
#define RES_ExtrasPreview_Clocks_Date_Text 0x0dad04d7
#define RES_ExtrasPreview_Clocks_Clock_View 0x0dad04d8
#define RES_ExtrasPreview_Lock_Template 0x0dad01e9
#define RES_ExtrasPreview_Lock_Caption_Template 0x0dad01eb
#define RES_ExtrasPreview_Background_Template 0x0dad01d8
#define RES_ExtrasPreview_Background_View 0x0dad04ce
#define RES_ExtrasPreview_Background_Image 0x0dad012d
#define RES_ExtrasPreview_Background_Bindings 0x0dad01d9
#define RES_VideosMenu_Screen 0x0dad0816
#define RES_VideosMenu_Items 0x00000033
#define RES_VideosMenu_Movies_Item 0x0dad0817
#define RES_VideosMenu_Rentals_Item 0x0dad0818
#define RES_VideosMenu_TVShows_Item 0x0dad0819
#define RES_VideosMenu_MusicVideos_Item 0x0dad081a
#define RES_VideosMenu_VideoPlaylists_Item 0x0dad081b
#define RES_VideosMenu_Settings_Item 0x0dad081c
#define RES_VideosMenu_Layout 0x0dad081d
#define RES_VideosMenu_Movies_Layout 0x0dad0820
#define RES_MusicMenu_Layout 0x0dad07d5
#define RES_MusicMenu_Songs_Layout 0x0dad07dc
#define RES_MediaMenuPreview_Template 0x0dad01b9
#define RES_MediaMenuPreview_Image_View 0x0dad01ba
#define RES_MediaMenuPreview_Caption_Text 0x0dad01bb
#define RES_MediaMenuPreview_Detail_Text 0x0dad01bc
#define RES_MediaMenuPreview_Detail2_Text 0x0dad01bd
#define RES_MediaMenuPreview_Bindings 0x0dad01be
#define RES_MainMenus_Main_Radio_Layout 0x0dad0799
#define RES_MainMenuPreview_Radio_Template 0x0dad079a
#define RES_MainMenuPreview_Radio_Image_View 0x0dad0aa6
#define RES_MainMenuPreview_Radio_Image 0x0dad0aa2
#define RES_MainMenuPreview_Radio_Frequency_Text 0x0dad0aa7
#define RES_MainMenuPreview_Radio_Badge_View 0x0dad0aa8
#define RES_MainMenuPreview_Radio_Badge_Image 0x0dad0aa1
#define RES_MenuPreview_Artwork_Template 0x0dad01c1
#define RES_MenuPreview_Artwork_View 0x0dad01c2
#define RES_MenuPreview_Artwork_Bindings 0x0dad01c3
#define RES_PhotosMenu_Screen 0x0dad0a3f
#define RES_MainMenu_Items 0x0000001a
#define RES_MusicMenu_Items 0x0000001b
#define RES_MainMenus_Main_Screen 0x0dad076e
#define RES_MainMenus_Main_Layout 0x0dad0798
#define RES_MainMenus_Music_Screen 0x0dad07c8
#define RES_ContextMenu_Screen 0x0dad01c9
#define RES_ContextMenu_Layout 0x0dad01ca
#define RES_Calendar_Empty_Layout 0x0dad0397
#define RES_Calendar_Empty_Instances 0x00000026
#define RES_Calendar_Empty_Template 0x0dad0372
#define RES_Calendar_Empty_Text 0x0dad0374
#define RES_ContextMenu_Items 0x00000001
#define RES_ContextMenu_Item 0x0dad01cb
#define RES_ContextMenu_StartGenius_String 0x0dad0117
#define RES_ContextMenu_BrowseArtist_String 0x0dad0118
#define RES_ContextMenu_BrowseAlbum_String 0x0dad0119
#define RES_ContextMenu_BrowseCompilation_String 0x0dad011a
#define RES_ContextMenu_AddToOnTheGo_String 0x0dad011b
#define RES_ContextMenu_RemoveFromOnTheGo_String 0x0dad011c
#define RES_ContextMenu_Cancel_String 0x0dad011d
#define RES_ContextMenu_Title_String 0x0dad011e
#define RES_ExtrasMenu_Screen 0x0dad01cc
#define RES_ExtrasMenu_Layout 0x0dad01d6
#define RES_ExtrasMenu_Stopwatch_Layout 0x0dad01ed
#define RES_MainMenu_Music_Item 0x0dad076f
#define RES_MainMenu_CoverFlow_Item 0x0dad0770
#define RES_MainMenu_GeniusMixes_Item 0x0dad0771
#define RES_MainMenu_Playlists_Item 0x0dad0772
#define RES_MainMenu_Artists_Item 0x0dad0773
#define RES_MainMenu_Albums_Item 0x0dad0774
#define RES_MainMenu_Compilations_Item 0x0dad0775
#define RES_MainMenu_Songs_Item 0x0dad0776
#define RES_MainMenu_Radio_Item 0x0dad0792
#define RES_MainMenu_Genres_Item 0x0dad0777
#define RES_MainMenu_Composers_Item 0x0dad0778
#define RES_MainMenu_Audiobooks_Item 0x0dad0779
#define RES_MainMenu_Search_Item 0x0dad077a
#define RES_MainMenu_Photos_Item 0x0dad0781
#define RES_MainMenu_Podcasts_Item 0x0dad0784
#define RES_MainMenu_iTunesU_Item 0x0dad0785
#define RES_MainMenu_Videos_Item 0x0dad077b
#define RES_MainMenu_VideoPlaylists_Item 0x0dad077c
#define RES_MainMenu_Movies_Item 0x0dad077d
#define RES_MainMenu_TVShows_Item 0x0dad077e
#define RES_MainMenu_MusicVideos_Item 0x0dad077f
#define RES_MainMenu_Rentals_Item 0x0dad0780
#define RES_MainMenu_Extras_Item 0x0dad0786
#define RES_MainMenu_Clocks_Item 0x0dad0787
#define RES_MainMenu_Alarms_Item 0x0dad0788
#define RES_MainMenu_Calendars_Item 0x0dad0789
#define RES_MainMenu_Contacts_Item 0x0dad078a
#define RES_MainMenu_Games_Item 0x0dad078b
#define RES_MainMenu_Notes_Item 0x0dad078c
#define RES_MainMenu_ScreenLock_Item 0x0dad078d
#define RES_MainMenu_Stopwatch_Item 0x0dad078e
#define RES_MainMenu_ShuffleSongs_Item 0x0dad0794
#define RES_MainMenu_Sleep_Item 0x0dad0796
#define RES_MusicMenu_CoverFlow_Item 0x0dad07c9
#define RES_MusicMenu_GeniusMixes_Item 0x0dad07ca
#define RES_MusicMenu_Playlists_Item 0x0dad07cb
#define RES_MusicMenu_Artists_Item 0x0dad07cc
#define RES_MusicMenu_Albums_Item 0x0dad07cd
#define RES_MusicMenu_Compilations_Item 0x0dad07ce
#define RES_MusicMenu_Songs_Item 0x0dad07cf
#define RES_MusicMenu_Radio_Item 0x0dad07d0
#define RES_MusicMenu_Genres_Item 0x0dad07d1
#define RES_MusicMenu_Composers_Item 0x0dad07d2
#define RES_MusicMenu_Audiobooks_Item 0x0dad07d3
#define RES_MusicMenu_Search_Item 0x0dad07d4
#define RES_ExtrasMenu_Clocks_Item 0x0dad01cd
#define RES_ExtrasMenu_Calendars_Item 0x0dad01ce
#define RES_ExtrasMenu_Contacts_Item 0x0dad01cf
#define RES_ExtrasMenu_Alarms_Item 0x0dad01d0
#define RES_ExtrasMenu_Games_Item 0x0dad01d1
#define RES_ExtrasMenu_Notes_Item 0x0dad01d2
#define RES_ExtrasMenu_ScreenLock_Item 0x0dad01d3
#define RES_ExtrasMenu_Stopwatch_Item 0x0dad01d4
#define RES_MediaLists_PodcastEpisodes_Screen 0x0dad07f8
#define RES_MediaLists_iTunesUEpisodes_Screen 0x0dad07fc
#define RES_MediaLists_Audiobooks_Screen 0x0dad07e1
#define RES_MediaLists_Movies_Screen 0x0dad0804
#define RES_MediaLists_Movies_WithTime_Layout 0x0dad0805
#define RES_MediaLists_TVEpisodes_Screen 0x0dad0802
#define RES_MediaLists_TVEpisodes_Plain_Layout 0x0dad0803
#define RES_MediaLists_MixedVideoTracks_Screen 0x0dad0812
#define RES_MediaLists_MixedVideoTracks_WithSubInfo_Layout 0x0dad0813
#define RES_MediaLists_Songs_Screen 0x0dad07e9
#define RES_MediaLists_Songs_Plain_Layout 0x0dad07ea
#define RES_MediaLists_Songs_WithArtistAndArtwork_Layout 0x0dad07ec
#define RES_MediaLists_NestedPlaylists_Screen 0x0dad0808
#define RES_MediaLists_Genius_Screen 0x0dad0844
#define RES_MediaLists_GeniusPlaylist_Screen 0x0dad0848
#define RES_MediaLists_Artists_Screen 0x0dad07f0
#define RES_MediaLists_Artists_Screen_Default 0x0dad07f1
#define RES_MediaLists_Albums_Screen 0x0dad07ed
#define RES_NowPlaying_Screen 0x0dad096c
#define RES_ExtrasMenu_Items 0x00000002
