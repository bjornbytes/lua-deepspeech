lovr.speech = require 'lua-deepspeech'

function lovr.load()
  lovr.speech.init({
    model = lovr.filesystem.getSource() .. '/deepspeech-0.9.3-models.pbmm'
  })

  -- Decode a sound file if provided
  if arg[1] then
    local sound = lovr.data.newSound(arg[1])
    local count = sound:getFrameCount()
    local samples = sound:getBlob():getPointer()
    local text = lovr.speech.decode(samples, count)
    print(text)
    lovr.event.quit()
    return
  end

  -- Otherwise set up microphone capture and feed audio to a speech decoder stream
  sink = lovr.data.newSound(4096, 'f32', 'mono', 16000)
  lovr.audio.setDevice('capture', 'default', sink)
  lovr.audio.start('capture')
  stream = lovr.speech.newStream()
end

function lovr.update(dt)
  if sink:getFrameCount() > 1024 then
    stream:feed(sink:getFrames())
    print(stream:decode())
  end
end
