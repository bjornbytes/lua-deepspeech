#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <deepspeech.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef BEAM_WIDTH
#define BEAM_WIDTH 500
#endif

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

static const char* stringifyError(int e) {
#define CASE(c) case c: return #c
  switch (e) {
    CASE(DS_ERR_OK);
    CASE(DS_ERR_NO_MODEL);
    CASE(DS_ERR_INVALID_ALPHABET);
    CASE(DS_ERR_INVALID_SHAPE);
    CASE(DS_ERR_INVALID_LM);
    CASE(DS_ERR_MODEL_INCOMPATIBLE);
    CASE(DS_ERR_FAIL_INIT_MMAP);
    CASE(DS_ERR_FAIL_INIT_SESS);
    CASE(DS_ERR_FAIL_INTERPRETER);
    CASE(DS_ERR_FAIL_RUN_SESS);
    CASE(DS_ERR_FAIL_CREATE_STREAM);
    CASE(DS_ERR_FAIL_READ_PROTOBUF);
    CASE(DS_ERR_FAIL_CREATE_SESS);
    CASE(DS_ERR_FAIL_CREATE_MODEL);
  }
  return NULL;
#undef CASE
}

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

static int lds_init(lua_State* L) {
  luaL_argcheck(L, lua_istable(L, 1), 1, "Expected config to be a table");

  if (state.modelState) {
    DS_FreeModel(state.modelState);
    state.modelState = NULL;
  }

  const char* model = NULL;
  const char* grammar = NULL;
  const char* trie = NULL;

  int type;

  lua_getfield(L, 1, "model");
  CHECK(lua_type(L, -1) == LUA_TSTRING, "config.model should be a string containing a path to the pbmm file");
  model = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "grammar");
  type = lua_type(L, -1);
  CHECK(type == LUA_TNIL || type == LUA_TSTRING, "config.grammar should be nil or a string");
  grammar = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "trie");
  type = lua_type(L, -1);
  CHECK((grammar == NULL) == (type == LUA_TNIL), "config.trie is required when config.grammar is set");
  CHECK((grammar == NULL) || type == LUA_TSTRING, "config.trie should be a string");
  trie = lua_tostring(L, -1);
  lua_pop(L, 1);

  int err = DS_CreateModel(model, BEAM_WIDTH, &state.modelState);
  if (err) {
    return luaL_error(L, "Failed to initialize DeepSpeech: %s", stringifyError(err));
  }

  if (grammar) {
    CHECK(DS_EnableDecoderWithLM(state.modelState, grammar, trie, 1.f, 1.f) == 0, "Failed to set grammar");
  }

  lua_pushboolean(L, true);
  return 1;
}

static int lds_destroy(lua_State* L) {
  if (state.modelState) {
    DS_FreeModel(state.modelState);
    state.modelState = NULL;
  }
  return 0;
}

static int lds_getSampleRate(lua_State* L) {
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  lua_pushinteger(L, DS_GetModelSampleRate(state.modelState));
  return 1;
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
  { "getSampleRate", lds_getSampleRate },
  { "decode", lds_decode },
  { "newStream", lds_newStream },
  { NULL, NULL },
};

static const luaL_Reg lds_stream_api[] = {
  { "feed", lds_stream_feed },
  { "decode", lds_stream_decode },
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
