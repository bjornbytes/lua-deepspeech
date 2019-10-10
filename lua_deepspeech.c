#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <deepspeech.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define N_CEP 26
#define N_CONTEXT 9
#define BEAM_WIDTH 500
#define SAMPLE_RATE 16000

#define CHECK(c, ...) if (!(c)) { return luaL_error(L, __VA_ARGS__); }

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

static int lds_init(lua_State* L) {
  luaL_argcheck(L, lua_istable(L, 1), 1, "Expected config to be a table");

  if (state.modelState) {
    DS_DestroyModel(state.modelState);
    state.modelState = NULL;
  }

  const char* model = NULL;
  const char* alphabet = NULL;
  const char* grammar = NULL;
  const char* trie = NULL;

  int type;

  lua_getfield(L, 1, "model");
  CHECK(lua_type(L, -1) == LUA_TSTRING, "config.model should be a string");
  model = lua_tostring(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, 1, "alphabet");
  CHECK(lua_type(L, -1) == LUA_TSTRING, "config.alphabet should be a string");
  alphabet = lua_tostring(L, -1);
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

  CHECK(DS_CreateModel(model, N_CEP, N_CONTEXT, alphabet, BEAM_WIDTH, &state.modelState) == 0, "DeepSpeech failed to initialize");

  if (grammar) {
    CHECK(DS_EnableDecoderWithLM(state.modelState, alphabet, grammar, trie, 1.f, 1.f) == 0, "Failed to set grammar");
  }

  lua_pushboolean(L, true);
  return 1;
}

static int lds_decode(lua_State* L) {
  size_t sampleCount;
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  const short* samples = lds_checksamples(L, 1, &sampleCount);
  CHECK(samples != NULL, "Expected a table or lightuserdata pointer for audio sample data");
  char* text = DS_SpeechToText(state.modelState, samples, sampleCount, SAMPLE_RATE);
  lua_pushstring(L, text);
  DS_FreeString(text);
  return 1;
}

static int lds_newStream(lua_State* L) {
  CHECK(state.modelState != NULL, "DeepSpeech is not initialized");
  lds_Stream* stream = (lds_Stream*) lua_newuserdata(L, sizeof(lds_Stream));
  CHECK(DS_SetupStream(state.modelState, SAMPLE_RATE, &stream->handle) == 0, "Could not create stream");
  luaL_getmetatable(L, "lds_Stream");
  lua_setmetatable(L, -2);
  return 1;
}

static int lds_stream_push(lua_State* L) {
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
  DS_SetupStream(state.modelState, SAMPLE_RATE, &stream->handle);
  return 1;
}

static int lds_stream_clear(lua_State* L) {
  lds_Stream* stream = (lds_Stream*) luaL_checkudata(L, 1, "lds_Stream");
  DS_DiscardStream(stream->handle);
  DS_SetupStream(state.modelState, SAMPLE_RATE, &stream->handle);
  return 0;
}

static const luaL_Reg lds_api[] = {
  { "init", lds_init },
  { "decode", lds_decode },
  { "newStream", lds_newStream },
  { NULL, NULL },
};

static const luaL_Reg lds_stream_api[] = {
  { "push", lds_stream_push },
  { "decode", lds_stream_decode },
  { "finish", lds_stream_finish },
  { "clear", lds_stream_clear },
  { NULL, NULL }
};

int luaopen_deepspeech(lua_State* L) {
  lua_newtable(L);
  luaL_register(L, NULL, lds_api);

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
