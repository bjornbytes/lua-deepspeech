#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <deepspeech.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define CHECK(c, ...) if (!(c)) { return luaL_error(L, __VA_ARGS__); }

#ifdef _WIN32
#define LDS_EXPORT __declspec(dllexport)
#else
#define LDS_EXPORT
#endif

static struct {
  ModelState* modelState;
  size_t bufferSize;
  short* buffer;
} state;

typedef struct {
  StreamingState* handle;
} lds_Stream;

static const short* lds_checksamples(lua_State* L, int index, size_t* count) {
  if (lua_istable(L, index)) {
    *count = lua_objlen(L, index);

    if (state.bufferSize < *count) {
      state.bufferSize += !state.bufferSize;
      do { state.bufferSize <<= 1; } while (state.bufferSize < *count);
      state.buffer = realloc(state.buffer, state.bufferSize);
    }

    for (size_t i = 0; i < *count; i++) {
      lua_rawgeti(L, index, i + 1);
      lua_Integer x  = lua_tointeger(L, -1);
      lua_pop(L, 1);

      if (x < INT16_MIN || x > INT16_MAX) {
        luaL_error(L, "Sample #%d (%d) is out of range [%d,%d]", i + 1, x, INT16_MIN, INT16_MAX);
      }

      state.buffer[i] = x;
    }

    return state.buffer;
  } else if (lua_type(L, index) == LUA_TLIGHTUSERDATA) {
    return *count = luaL_checkinteger(L, index + 1), lua_touserdata(L, index);
  }

  return NULL;
}

static void lds_pushmetadata(lua_State* L, Metadata* metadata) {
  lua_createtable(L, metadata->num_transcripts, 0);
  for (int i = 0; i < metadata->num_transcripts; i++) {
    const CandidateTranscript* transcript = &metadata->transcripts[i];
    lua_createtable(L, 0, 3);

    lua_pushnumber(L, transcript->confidence);
    lua_setfield(L, -2, "confidence");

    lua_createtable(L, transcript->num_tokens, 0);
    for (int j = 0; j < transcript->num_tokens; j++) {
      lua_pushnumber(L, transcript->tokens[j].start_time);
      lua_rawseti(L, -2, j + 1);
    }
    lua_setfield(L, -2, "times");

    lua_createtable(L, transcript->num_tokens, 0);
    for (int j = 0; j < transcript->num_tokens; j++) {
      lua_pushstring(L, transcript->tokens[j].text);
      lua_rawseti(L, -2, j + 1);
    }
    lua_setfield(L, -2, "tokens");

    lua_rawseti(L, -2, i + 1);
  }
}

static int lds_init(lua_State* L) {
  luaL_argcheck(L, lua_istable(L, 1), 1, "Expected config to be a table");

  if (state.modelState) {
    DS_FreeModel(state.modelState);
    state.modelState = NULL;
  }

  const char* model = NULL;
  const char* scorer = NULL;

  lua_getfield(L, 1, "model");
  CHECK(lua_type(L, -1) == LUA_TSTRING, "config.model should be a string containing a path to the pbmm file");
  model = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "scorer");
  int type = lua_type(L, -1);
  CHECK(type == LUA_TNIL || type == LUA_TSTRING, "config.scorer should be nil or a string");
  scorer = lua_tostring(L, -1);
  lua_pop(L, 1);

  int err = DS_CreateModel(model, &state.modelState);
  if (err) {
    lua_pushboolean(L, false);
    char* message = DS_ErrorCodeToErrorMessage(err);
    lua_pushstring(L, message);
    DS_FreeString(message);
    return 2;
  }

  lua_getfield(L, 1, "beamWidth");
  if (!lua_isnil(L, -1)) {
    DS_SetModelBeamWidth(state.modelState, luaL_checkinteger(L, -1));
  }
  lua_pop(L, 1);

  if (scorer) {
    CHECK(DS_EnableExternalScorer(state.modelState, scorer) == 0, "Failed to set scorer");

    lua_getfield(L, 1, "alpha");
    float alpha = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, 1, "beta");
    float beta = lua_tonumber(L, -1);
    lua_pop(L, 1);

    if (alpha != 0.f || beta != 0.f) {
      CHECK(DS_SetScorerAlphaBeta(state.modelState, alpha, beta) == 0, "Failed to set scorer alpha/beta");
    }
  }

  lua_pushboolean(L, true);
  lua_pushinteger(L, DS_GetModelSampleRate(state.modelState));
  return 2;
}

static int lds_destroy(lua_State* L) {
  if (state.modelState) {
    DS_FreeModel(state.modelState);
    state.modelState = NULL;
  }
  state.bufferSize = 0;
  free(state.buffer);
  return 0;
}

static int lds_decode(lua_State* L) {
  size_t sampleCount;
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  const short* samples = lds_checksamples(L, 1, &sampleCount);
  CHECK(samples != NULL, "Expected a table or lightuserdata pointer for audio sample data");
  char* text = DS_SpeechToText(state.modelState, samples, sampleCount);
  lua_pushstring(L, text);
  DS_FreeString(text);
  return 1;
}

static int lds_analyze(lua_State* L) {
  size_t sampleCount;
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  const short* samples = lds_checksamples(L, 1, &sampleCount);
  CHECK(samples != NULL, "Expected a table or lightuserdata pointer for audio sample data");
  uint32_t limit = luaL_optinteger(L, lua_istable(L, 1) ? 2 : 3, 3);
  Metadata* metadata = DS_SpeechToTextWithMetadata(state.modelState, samples, sampleCount, limit);
  lds_pushmetadata(L, metadata);
  DS_FreeMetadata(metadata);
  return 1;
}

static int lds_boost(lua_State* L) {
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  const char* word = luaL_checkstring(L, 1);
  float boost = luaL_checknumber(L, 2);
  DS_AddHotWord(state.modelState, word, boost);
  return 0;
}

static int lds_unboost(lua_State* L) {
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  const char* word = lua_tostring(L, 1);
  if (word) {
    DS_EraseHotWord(state.modelState, word);
  } else {
    DS_ClearHotWords(state.modelState);
  }
  return 0;
}

static int lds_newStream(lua_State* L) {
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  lds_Stream* stream = (lds_Stream*) lua_newuserdata(L, sizeof(lds_Stream));
  CHECK(DS_CreateStream(state.modelState, &stream->handle) == 0, "Could not create stream");
  luaL_getmetatable(L, "lds_Stream");
  lua_setmetatable(L, -2);
  return 1;
}

static int lds_stream_feed(lua_State* L) {
  size_t sampleCount;
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  const short* samples = lds_checksamples(L, 2, &sampleCount);
  CHECK(samples != NULL, "Expected a table or lightuserdata pointer for audio sample data");
  DS_FeedAudioContent(stream->handle, samples, sampleCount);
  return 0;
}

static int lds_stream_decode(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  char* text = DS_IntermediateDecode(stream->handle);
  lua_pushstring(L, text);
  DS_FreeString(text);
  return 1;
}

static int lds_stream_analyze(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  uint32_t limit = luaL_optinteger(L, 2, 3);
  Metadata* metadata = DS_IntermediateDecodeWithMetadata(stream->handle, limit);
  lds_pushmetadata(L, metadata);
  DS_FreeMetadata(metadata);
  return 1;
}

static int lds_stream_finish(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  char* text = DS_FinishStream(stream->handle);
  lua_pushstring(L, text);
  DS_FreeString(text);
  DS_CreateStream(state.modelState, &stream->handle);
  return 1;
}

static int lds_stream_clear(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  DS_FreeStream(stream->handle);
  DS_CreateStream(state.modelState, &stream->handle);
  return 0;
}

static int lds_stream_destroy(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  DS_FreeStream(stream->handle);
  return 0;
}

static const luaL_Reg lds_api[] = {
  { "init", lds_init },
  { "decode", lds_decode },
  { "analyze", lds_analyze },
  { "boost", lds_boost },
  { "unboost", lds_unboost },
  { "newStream", lds_newStream },
  { NULL, NULL },
};

static const luaL_Reg lds_stream_api[] = {
  { "feed", lds_stream_feed },
  { "decode", lds_stream_decode },
  { "analyze", lds_stream_analyze },
  { "finish", lds_stream_finish },
  { "clear", lds_stream_clear },
  { "__gc", lds_stream_destroy },
  { NULL, NULL }
};

LDS_EXPORT int luaopen_deepspeech(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, NULL, lds_api);

  // Add sentinel userdata to free the model state on GC
  lua_newuserdata(L, sizeof(void*));
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, lds_destroy);
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, -2);
  lua_setfield(L, -2, "");

  if (luaL_newmetatable(L, "lds_Stream")) {
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_register(L, NULL, lds_stream_api);
    lua_pop(L, 1);
  } else {
    return luaL_error(L, "Could not register lds_Stream metatable!");
  }

  return 1;
}
