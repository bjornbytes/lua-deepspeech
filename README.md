lua-deepspeech
===

Lua bindings for [DeepSpeech](https://github.com/Mozilla/DeepSpeech), an open source speech
recognition library.  Intended for use with [LÖVR](https://lovr.org) and [LÖVE](https://love2d.org),
but it should work with any Lua program that has audio samples in a table or a lightuserdata.

Here's a simple example of using it to do speech-to-text on an audio file:

```lua
lovr.speech = require 'lua-deepspeech'

function lovr.load()
  lovr.speech.init({ model = '/path/to/model.pbmm' })

  local sound = lovr.data.newSoundData('speech.ogg')
  local samples = sound:getBlob():getPointer()
  local count = sound:getSampleCount()

  print(lovr.speech.decode(samples, count))
end
```

DeepSpeech Setup
---

- Download the DeepSpeech native client library.  It can be found on the [DeepSpeech releases page](https://github.com/Mozilla/DeepSpeech/releases/latest)
  and will be named something like `native_client.<arch>.<flavor>.<os>.tar.xz`.  The most recent
  version tested is **0.9.3**.  It should contain a `deepspeech.h` file and a platform-specific
  library, like a .so or .dll file.
- Download the speech recognition model from the same release page.  It's a huge `pbmm` file.

> Note: There are multiple flavors of the native client.  The `cpu` flavor runs on the CPU, the
`gpu` flavor runs on the GPU with CUDA, and the `tflite` flavor can use the smaller tflite model
instead of the pbmm one.

Building
---

Once you have the DeepSpeech files downloaded, build the Lua bindings in this repository.  You can
download prebuilt files from the releases page or build them using CMake.  If you're using LÖVR you
can also add this repository to the `plugins` folder and rebuild.  The `DEEPSPEECH_PATH` variable
needs to be set to the path to the native client.

```sh
$ mkdir build
$ cd build
$ cmake .. -DDEEPSPEECH_PATH=/path/to/native_client
$ cmake --build .
```

This should output `lua-deepspeech.dll` or `lua-deepspeech.so`.

The deepspeech native_client library needs to be placed somewhere that it can be loaded at runtime
and the lua-deepspeech library needs to be somewhere that it can be required by Lua.  For LÖVR both
of these can be put next to the lovr executable (building as a plugin will take care of this).
For other engines it will probably be different.

Usage
---

First, require the module:

```lua
local speech = require 'lua-deepspeech'
```

It returns a table with the library's functionality.

```lua
success = speech.init(options)
```

The library must be initialized with an options table.  The table can contain the following options:

- `options.model` should be a full path to the deepspeech model file (pbmm).  If this file is stored
  in a zip archive fused to the executable it will need to be written to disk first.
- `options.grammar` TODO

```lua
sampleRate = speech.getSampleRate()
```

Returns the sample rate the model was trained on, in Hz.  This is usually 16000Hz.  Audio
information passed to the library should use this sample rate.

```lua
text = speech.decode(table)
text = speech.decode(pointer, count)
```

This functions performs speech-to-text.  A table of audio samples can be provided, or a
lightuserdata pointer with a sample count.

In all cases the audio data must be formatted as **signed 16 bit mono** samples at the appropriate
sample rate (usually 16,000Hz, use `speech.getSampleRate` to check).

A string is returned with the decoded text.

### Streams

A stream object can be used to decode audio in real time as it arrives.  Usually you'd use this with
audio coming from a microphone.

```
stream = speech.newStream()
```

Creates a new Stream.

```
Stream:feed(table)
Stream:feed(pointer, count)
```

Feeds audio to the Stream.  Accepts the same arguments as `speech.decode`.

```
text = Stream:decode()
```

Performs an intermediate decode on the audio data fed to the Stream, returning the decoded text.
Additional audio can continue to be fed to the Stream after this function is called.

```
text = Stream:finish()
```

Finishes and resets the Stream, returning the final decoded text.

```
Stream:clear()
```

Resets the Stream, erasing all audio that has been fed to it.

License
---

MIT, see [`LICENSE`](LICENSE) for details.