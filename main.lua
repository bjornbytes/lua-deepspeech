lovr.speech = require 'lua-deepspeech'

function lovr.load()
  lovr.speech.init({
    model = lovr.filesystem.getSource() .. '/deepspeech-0.9.3-models.pbmm'
  })

  -- Decode a sound file if provided
  if arg[1] then
    local soundData = lovr.data.newSoundData(arg[1])
    local sampleCount = soundData:getSampleCount()
    local samples = soundData:getBlob():getPointer()
    local text = lovr.speech.decode(samples, sampleCount)
    print(text)
    lovr.event.quit()
    return
  end

  -- Otherwise create a Microphone and feed audio to a speech decoder stream
  local microphones = lovr.audio.getMicrophoneNames()
  microphone = lovr.audio.newMicrophone(microphones[1], 4096, 16000, 16, 1)
  assert(microphone, 'Could not create Microphone')
  microphone:startRecording()
  stream = lovr.speech.newStream()
end

function lovr.update(dt)
  if microphone:getSampleCount() > 1024 then
    local soundData = microphone:getData()
    local sampleCount = soundData:getSampleCount()
    local pointer = soundData:getBlob():getPointer()
    stream:feed(pointer, sampleCount)
    print(stream:decode())
  end
end
