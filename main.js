// import WaveSurfer from 'wavesurfer.js'
import WaveSurfer from 'https://unpkg.com/wavesurfer.js@7/dist/wavesurfer.esm.js'
import RegionsPlugin from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/regions.esm.js'
import Timeline from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/timeline.esm.js'
import Record from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/record.esm.js'

// If using a bundler (Vite, Webpack, or Parcel): Run: 'npm install jszip'. Then use:
    //      import JSZip from "jszip"
// If using plain HTML/JS file (no bundler): Include the library via a CDN in HTML <head>:
    //      <script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.min.js"></script>


/////         Store UI Elements         /////
const fileInput = document.getElementById('fileInput')
const recordBtn = document.getElementById('recordBtn')

const questionBtn = document.getElementById('question')

const zoomInBtn = document.getElementById('zoomInBtn')
const zoomOutBtn = document.getElementById('zoomOutBtn')
const zoomSlider = document.getElementById('zoomSlider')

const drawBtn = document.getElementById('drawBtn')
const eraseBtn = document.getElementById('eraseBtn')
const saveBtn = document.getElementById('saveBtn')
const loadDataBtn = document.getElementById('loadDataBtn')
// const fishFileInput = document.getElementById('fishFileInput');
const clearBtn = document.getElementById('clearBtn')
const sendBtn = document.getElementById('sendBtn')

const headBodyBtn = document.getElementById('headBodyChoice')
const headBodyChoice = document.getElementById('headBodyChoice')
const headPic = document.getElementById('headPic')
const headControlTitle = document.getElementById('headControlTitle')
const tailPic = document.getElementById('tailPic')
const tailControlTitle = document.getElementById('tailControlTitle')
// const Btn = document.getElementById('Btn')

//*****      *****     *****//
const playButton = document.querySelector('#play')
const forwardButton = document.querySelector('#forward')
const backButton = document.querySelector('#backward')
//*****      *****     *****//

/////         Global Variables         /////
const minMouthTime = 0.001 // seconds
const minBodyTime = 0.002 // seconds
const minHeadTime = 1.5 // seconds
const minTailTime = 0.5 // seconds


let audioWave_full = null   // Full audio Waveform - Greyed out
let audioWave_mouth = null  // Waveform in Mouth area
let audioWave_body = null   // Waveform in Body area
let regions_mouth = null
let regions_body = null
let recordPlugin = null
// let audioFile = null
let audioBlob = null
let editMode = "draw" //  "draw" <--> "erase"
let bodyMode = "head" //  "head" <--> "tail"
let bodyDrawColor = 'rgba(50,255,50,0.25)' // () => (bodyMode == "head") ? 'rgba(50,255,50,0.25)' : 'rgba(255,50,50,0.25)'

// default zoom (px per second) — match slider default value
const DEFAULT_ZOOM = (zoomSlider && +zoomSlider.value) || 100

//******  ******  ******  ******  ******//
// helper to avoid infinite loops when syncing seek/scroll
let isSyncingScroll = false
function withSyncGuard(fn){
  if (isSyncingScroll) return
  isSyncingScroll = true
  try { fn() } finally { isSyncingScroll = false }
}
//******  ******  ******  ******  ******//

document.addEventListener("DOMContentLoaded", () => {
  initializeWaveSurfers();
})
drawBtn.addEventListener("click", setEditMode)
eraseBtn.addEventListener("click", setEditMode)
headBodyChoice.addEventListener("click", swapBodyMode)


/////         SETUP AUDIO WAVEFORM STUFF          /////
function initializeWaveSurfers(){
  const timeline_audiowave = Timeline.create({
    container: document.getElementById('overviewTimeline'),
    height: 18,
    insertPosition: 'beforebegin',
    timeInterval: 0.2,
    primaryLabelInterval: 5,
    secondaryLabelInterval: 1,
    style: {
      fontSize: '18px',
      color: '#2D5B88',
    }
  })

  // 1) Create the full / master player (this will hold the single HTMLMediaElement)
  audioWave_full = WaveSurfer.create({
    container: document.getElementById('audioOverview'),
    waveColor: '#bbb',
    progressColor: '#999',
    // no url here — audio loaded with blobs --> (upload/record) via loadBlob()
    minPxPerSec: 100,
    dragToSeek: true,
    plugins: [
      timeline_audiowave
    ]
  })

  // 2) create two independent Regions plugin instances — one per editable view
  regions_mouth = RegionsPlugin.create({
    drag: true,
    resize: true
  })
  regions_body = RegionsPlugin.create({
    drag: true,
    resize: true
  })

  // 3) Create the mouth and body players, *sharing the same media element* as the master.
  //    This is the trick that keeps play/pause/seek in sync automatically.
  audioWave_mouth = WaveSurfer.create({
    container: document.getElementById('mouthControl'),
    waveColor: 'rgb(100, 125, 255)',
    progressColor: 'rgb(100, 125, 255)',
    minPxPerSec: DEFAULT_ZOOM,
    dragToSeek: false,
    interact: false,
    media: audioWave_full.getMediaElement(),   // <-- correct way
    plugins: [ regions_mouth ]
  })

  // Create body using the master's media element so playback/seek is tied:
  audioWave_body = WaveSurfer.create({
    container: document.getElementById('bodyControl'),
    waveColor: 'rgb(50, 125, 255)',
    progressColor: 'rgb(50, 125, 255)',
    minPxPerSec: DEFAULT_ZOOM,
    dragToSeek: false,
    interact: false,
    media: audioWave_full.getMediaElement(),   // <-- correct way
    plugins: [ regions_body ]
  })

  // 4) Record plugin: register on the master; when recording finishes we get a Blob and load it.
  recordPlugin = Record.create({
    renderRecordedAudio: true,      // show the recorded waveform in the master
    scrollingWaveform: true,
    audioBitsPerSecond: 128000,
    mimeType: 'audio/webm'          // change if you prefer wav/ogg
  })
  audioWave_full.registerPlugin(recordPlugin)

  // When recording ends, the plugin will emit a blob we can load into the master (and the linked views auto-update)
  recordPlugin.on('record-end', (blob) => {
    // resetPage() // ****** Clear all waveforms/regions  ******
    loadAudioBlob(blob)
  })

  // 5) wire up UI controls
  playButton.onclick = () => audioWave_full.playPause()
  forwardButton.onclick = () => audioWave_full.skip(5)
  backButton.onclick = () => audioWave_full.skip(-5)

  // zoom slider — apply only to editable (mouth/body) views. full stays static.
  if (zoomSlider){
    zoomSlider.addEventListener('input', (e) => {
      const zoom = e.target.valueAsNumber
      if (audioWave_mouth) audioWave_mouth.zoom(zoom)
      if (audioWave_body)  audioWave_body.zoom(zoom)
      /* const minPxPerSec = e.target.valueAsNumber
      audioWave_mouth.zoom(minPxPerSec)
      audioWave_body.zoom(minPxPerSec) */
    })
  }

  // sync seek events so other instances always reflect user interactions
  // (they already share mediaElement, but these ensure UI updates in all cases)
  audioWave_mouth.on('seek', (pos) => {
    withSyncGuard(()=> {
      audioWave_body.seekTo(pos)
      // do not force the master to re-load — it uses the same media element
    })
  })
  audioWave_body.on('seek', (pos) => {
    withSyncGuard(()=> {
      audioWave_mouth.seekTo(pos)
    })
  })

/***      ***     Usable for styling waveforms with CSS      ***     ***/
  const mouthWrapper = audioWave_mouth.getWrapper()
  const bodyWrapper  = audioWave_body.getWrapper()
/***      ***     ***      ***     ***      ***     ***      ***     ***/

  const mouthScrollWrapper = mouthWrapper.parentElement
  const bodyScrollWrapper  = bodyWrapper.parentElement
  
  mouthScrollWrapper.addEventListener('scroll', (ev) => {
    if (isSyncingScroll) return
    withSyncGuard(() => { bodyScrollWrapper.scrollLeft = ev.target.scrollLeft })
  }, { passive: true })
  bodyScrollWrapper.addEventListener('scroll', (ev) => {
    if (isSyncingScroll) return
    withSyncGuard(() => { mouthScrollWrapper.scrollLeft = ev.target.scrollLeft })
  }, { passive: true })


  /////         REGIONS   -->   SELECTING, EDITING, REMOVING         /////
  regions_mouth.enableDragSelection({
    color: 'rgba(50,125,255,0.25)',
    minLength: minMouthTime,
  })
  regions_body.enableDragSelection({
    color: 'rgba(225, 225, 50, 0.25)', // bodyDrawColor,
    minLength: minBodyTime,
  })

 regions_mouth.on('region-clicked', (region) => {
    (editMode === "erase") ? region.remove() : null
  })
  regions_body.on('region-clicked', (region) => {
    (editMode === "erase") ? region.remove() : null
  })

  // React to region events (create / update / remove)
  regions_mouth.on('region-created', (region) => {
    const regions = Object.values(regions_mouth.getRegions())
    enforceMinLength(region, minMouthTime)
    checkOverlaps(region, regions)

    region.motor = "mouth"
    // Add downward slope //
  })

  regions_body.on('region-created', (region) => {
    region.setOptions({ color: bodyDrawColor });
    const regions = Object.values(regions_body.getRegions())
    // console.log(overlaps(region, regions)) // Prints nothing?
    enforceMinLength(region, minBodyTime)
    checkOverlaps(region, regions)

    region.motor = bodyMode
    // Add downward slope //
  })
  //  ******

  function enforceMinLength(region, minLength) {
    if ((region.end - region.start) < minLength) {
      region.setOptions({
        end: region.start + minLength
      })
    }
  }
  function checkOverlaps(region, regions) {
    let closestOverlapStart = Infinity

    for (const r of regions) {
      if (r === region) continue

      if (region.start < r.end && region.end > r.start) {
        closestOverlapStart = Math.min(closestOverlapStart, r.start)
      }
    }

    if (closestOverlapStart !== Infinity) {
      region.setOptions({
        end: closestOverlapStart
      })
    }
  }
  //  ******


  /////         LOADING / RECORDING NEW AUDIO         /////
  fileInput.addEventListener('change', function(e){ // file input: when a file is selected, load it into the master
    const file = this.files && this.files[0]
    if (!file) return

    // resetPage() // Clear all waveforms/regions
    
    // loadFile(file);
    const reader = new FileReader()
    reader.onload = (evt) => {
    const blob = new Blob([new Uint8Array(evt.target.result)])
    loadAudioBlob(blob)
  }
  reader.readAsArrayBuffer(file)
  }, false)

  recordBtn.addEventListener('click', async () => {// record button: toggle recording through the plugin API
    try {
      if (!recordPlugin) return
      if (recordPlugin.isRecording) {
        const blob = recordPlugin.stopRecording() // plugin returns the final blob
        // blob is already handled by record-end handler above, but good to have it here too
        // audioWave_full.loadBlob(blob)
      } else {
        await recordPlugin.startRecording()
      }
    } catch (err) {
      console.error('record error', err)
    }
  })

  //  ******
  audioWave_full.on('ready', () => {
    const peaks = audioWave_full.exportPeaks()
    const duration = audioWave_full.getDuration()

    audioWave_mouth.load(null, peaks, duration)
    audioWave_body.load(null, peaks, duration)
  })
  //  ******

}

function loadAudioBlob(blob){
  audioWave_full.loadBlob(blob)
  audioBlob = blob  //  ***
}


/////         SWAPING HEAD/TAIL EDIT MODE         /////
function swapBodyMode(){
  if (bodyMode == "head"){ // Change to TAIL
    bodyMode = "tail"
    bodyDrawColor = 'rgba(255,50,50,0.25)'
    headPic.classList.add("hidden")
    tailPic.classList.remove("hidden")
    headControlTitle.classList.add("inactive")
    headControlTitle.style.textShadow = "none"
    tailControlTitle.classList.remove("inactive")
    tailControlTitle.style.textShadow = "0px 0px 10px rgba(200, 25, 25, 0.5), 0px 0px 20px rgba(200, 25, 25, 0.85)"

  } else{       //  Change to HEAD
    bodyMode = "head"
    bodyDrawColor = 'rgba(50,255,50,0.25)'
    headPic.classList.remove("hidden")
    tailPic.classList.add("hidden")
    headControlTitle.classList.remove("inactive")
    tailControlTitle.style.textShadow = "none"
    tailControlTitle.classList.add("inactive")
    headControlTitle.style.textShadow = "0px 0px 10px rgba(25, 200, 25, 0.5), 0px 0px 20px rgba(25, 200, 25, 0.85)"
  }
}


/////         CHANGING DRAW/ERASE EDIT MODE         /////
function setEditMode(source){
  if(source.target.id == "drawBtn"){
    editMode = "draw"
    drawBtn.classList.add("drawStyleSelected")
    eraseBtn.classList.remove("drawStyleSelected")
  } else {
    editMode = "erase"
    drawBtn.classList.remove("drawStyleSelected")
    eraseBtn.classList.add("drawStyleSelected")
  }
}


/////               SAVE/LOAD --> MUSIC + MOTOR DATA              /////
saveBtn.addEventListener("click", async() => {
  const zip = new JSZip();
  
  // Add the JSON data as a string
  const motorData = compileEvents();
  zip.file("motor-data.json", JSON.stringify(motorData, null, 2));
  
  // Save the original audio blob --> preserve the original file's type inside the zip
  const extension = (audioBlob.type.split('/')[1] || 'bin').split(';')[0].trim();
  zip.file(`audio.${extension}`, audioBlob);
  
  // Generate the zip file
  const content = await zip.generateAsync({type:"blob"});
  
  // Trigger automatic download
  const link = document.createElement("a");
  link.href = URL.createObjectURL(content);
  link.download = "my_fish_dance.fish"; //  *** Change name to use original audio file
  link.click();
})

loadDataBtn.addEventListener("click", () => {
  const fishFileInput = document.createElement("input");
  fishFileInput.type = "file";
  fishFileInput.accept = ".fish";
  // fishFileInput.style.display = "none";
  // document.body.appendChild(fishFileInput); // Need to append to avoid possible browser security
  
  fishFileInput.addEventListener("change", async(e) => {
    const file = e.target.files[0];
    if (!file) return;
  
    try {
      const zip = await JSZip.loadAsync(file);
      const dataFile = zip.file("motor-data.json");
      const audioFileName = Object.keys(zip.files).find(name => name.startsWith("audio."));
      const audioFile = audioFileName ? zip.file(audioFileName) : null;

      console.log(zip)
      // Check if the file exists in the zip before trying to read it
      if (!dataFile || !audioFile) {
        alert("Error: This doesn't look like a valid Billy Bass project file!");
        return;
      }
      
      resetPage() // Clear all waveforms/regions
  
      // Extract motor data and audio file
      const motorData = JSON.parse(await dataFile.async("text"));
      const extractedAudioBlob = await audioFile.async("blob");
      
      loadAudioBlob(extractedAudioBlob); // Use existing function for waveform + global variable update!
      loadedData(motorData); // Update the timeline/motor data
      // loadFile(audioUrl)
      // console.log(motorData)
  
    } catch (err) {
      console.error("Failed to load zip:", err);
      alert("Something went wrong reading the file.");
    }
  })

  fishFileInput.click(); // Needed to trigger the above addEventListener
})

// loadDataBtn.addEventListener("click", () => {
//   resetPage();
//   fishFileInput.click();
// });
// fishFileInput.addEventListener("change", async(e) => {
//   const file = e.target.files[0];
//   if (!file) return;

//   try {
//     const zip = await JSZip.loadAsync(file);

//     // Check if the file exists in the zip before trying to read it
//     const dataFile = zip.file("motor-data.json");
//     // const audioFile = zip.file("audio.bin");
//     const audioFileName = Object.keys(zip.files).find(name => name.startsWith("audio."));
//     const audioFile = audioFileName ? zip.file(audioFileName) : null;
//     console.log(zip)
//     if (!dataFile || !audioFile) {
//       alert("Error: This doesn't look like a valid Billy Bass project file!");
//       return;
//     }

//     // 1. Extract motor data and audio file
//     const motorData = JSON.parse(await dataFile.async("text"));
//     const extractedAudioBlob = await audioFile.async("blob");

//     // Use your existing function so the waveform and global variable update!
//     loadAudioBlob(extractedAudioBlob); 
    
//     // Update your timeline/motor data variable
//     // (Assuming you have a global variable for this)
//     loadedData(motorData);

//     console.log("Project Loaded Successfully!", motorData);
//     // loadFile(audioUrl)
//     // console.log(motorData)

//   } catch (err) {
//     console.error("Failed to load zip:", err);
//     alert("Something went wrong reading the file.");
//   } finally {
//     // Reset the input so you can load the same file twice if you wanted to
//     fishFileInput.value = "";
//   }
//   // return { motorData, audioUrl };
// })

function resetPage(){
  audioWave_full.empty();
  audioWave_mouth.empty();
  audioWave_body.empty();
  regions_mouth.clearRegions();
  regions_body.clearRegions();
  if (bodyMode == "tail"){
    swapBodyMode();
  }
}

function loadedData(motorData, audio){
  const ordered = Object.groupBy(motorData, ({motor}) => motor );

  // Wait until Wavesurfer waveforms are ready
  audioWave_body.once('ready', () => { // 'audioWave_mouth' should be ready before 'audioWave_body' <-- ?is true?
    restoreRegions(ordered.mouth, 'mouth', 'rgba(50,125,255,0.25)', minMouthTime);
    restoreRegions(ordered.head, 'head', 'rgba(50,255,50,0.25)', minBodyTime); // minHeadTime);
    swapBodyMode(); // Enter 'tail' mode
    restoreRegions(ordered.tail, 'tail', 'rgba(255,50,50,0.25)', minBodyTime); // minTailTime);
    swapBodyMode(); // Return to 'head' mode
  })

  function restoreRegions(events, body, color, minLength) {
    if (!events) return;
    const whichRegion = (body=='mouth') ? regions_mouth : regions_body
    if (body=='head'){
      
    }
    let startTime = null;

    events.forEach(e => {
      if (e.state == 1){
        startTime = e.t;
      } else if (startTime !== null){
        const newRegion = whichRegion.addRegion({
          start: startTime/1000,  // Change time from milliseconds --> seconds
          end: e.t/1000,  // Change time from milliseconds --> seconds
          color: color,
          minLength: minLength,
          drag: true,
          resize: true
        })
        newRegion.motor = body;
        startTime = null; // Reset
      }
    })
  }
  
}


/////               SEND DATA TO FISH              /////
sendBtn.addEventListener("click", async () => { // send via bluetooth
  const audioBuffer = await getAudioBlob(audioBlob);
    console.log("Audio size:", audioBuffer.byteLength, "bytes");
    const allEvents = compileEvents(); // Stores all motor movement data in array
    // console.log(allEvents);
    console.log("Events:", allEvents.length);
    const packet = buildPacket(allEvents, audioBuffer);
    console.log("Total packet size:", packet.byteLength, "bytes");

  const device = await navigator.bluetooth.requestDevice({
      filters: [{ services: [0xBA55] }],
      optionalServices: ['b160ba55-aaaa-0117-3650-005006019920']
  });

  const server = await device.gatt.connect();
  
  // Force the MTU request and actually wait for it
  try {
      if (device.gatt.requestMTU) {
          await device.gatt.requestMTU(512);
          console.log("MTU set to 512");
      }
  } catch(e) {
      console.log("MTU request failed, continuing with default", e);
  }

  // const service = await server.getPrimaryService(0xBA55);
  const service = await server.getPrimaryService('b160ba55-aaaa-0117-3650-005006019920');
  const characteristic = await service.getCharacteristic('0abc1230-0021-0021-0021-333444455555');

  await sendBinary(packet, characteristic);
})
function compileEvents(){
  const events = []
  regions_mouth.regions.forEach(region => {
    events.push({
      t: Math.round(region.start * 1000), // Convert time to integer in milliseconds
      motor: region.motor,
      state: 1,
      pwm: 50     // 0-100
    })
    events.push({
      t: Math.round(region.end * 1000), // Convert time to integer in milliseconds
      motor: region.motor,
      state: 0,
      pwm: 0      // 0-100
    })
  })
  regions_body.regions.forEach(region => {
    events.push({
      t: Math.round(region.start * 1000), // Convert time to integer in milliseconds
      motor: region.motor,
      state: 1,
      pwm: 50     // 0-100
    })
    events.push({
      t: Math.round(region.end * 1000), // Convert time to integer in milliseconds
      motor: region.motor,
      state: 0,
      pwm: 0      // 0-100
    })
  })

  events.sort((a,b) => a.t - b.t)
  return events
}

const MOTOR_MAP = {
    mouth: 0,
    head: 1,
    tail: 2
};
function buildPacket(events, audioArrayBuffer) {
    const numEvents = events.length;
    const audioSize = audioArrayBuffer.byteLength;

    const headerSize = 2 + 4; // uint16 + uint32
    const eventSize = 7;      // per event

    const totalSize = headerSize + (numEvents * eventSize) + audioSize;

    const buffer = new ArrayBuffer(totalSize);
    const view = new DataView(buffer);

    let offset = 0;

    // HEADER
    view.setUint16(offset, numEvents, true); offset += 2;
    view.setUint32(offset, audioSize, true); offset += 4;

    // EVENTS
    events.forEach(e => {
        view.setUint32(offset, e.t, true); offset += 4;
        view.setUint8(offset, MOTOR_MAP[e.motor]); offset += 1;
        view.setUint8(offset, e.state); offset += 1;
        view.setUint8(offset, e.pwm); offset += 1;
    });

    // AUDIO
    const audioBytes = new Uint8Array(audioArrayBuffer);
    new Uint8Array(buffer, offset).set(audioBytes);

    return buffer;
}

async function getAudioBlob(file) {
    // const audioContext = new AudioContext({ sampleRate: 44100 });
    const audioContext = new AudioContext({ sampleRate: 16000 }); // lower = smaller file, still sounds fine
    const arrayBuffer = await file.arrayBuffer();
    
    // Decodes MP3, WAV, M4A, OGG — anything the browser supports
    const audioBuffer = await audioContext.decodeAudioData(arrayBuffer);
    
    // Mix down to mono (Billy Bass only has one speaker)
    const numChannels = audioBuffer.numberOfChannels;
    const length = audioBuffer.length;
    const inputData = [];
    for (let c = 0; c < numChannels; c++) {
        inputData.push(audioBuffer.getChannelData(c));
    }
    
    // Convert float32 samples to int16 PCM
    const pcm = new Int16Array(length);
    for (let i = 0; i < length; i++) {
        // Average all channels for mono mixdown
        let sample = 0;
        for (let c = 0; c < numChannels; c++) {
            sample += inputData[c][i];
        }
        sample /= numChannels;
        // Clamp and convert float32 (-1.0 to 1.0) to int16 (-32768 to 32767)
        sample = Math.max(-1, Math.min(1, sample));
        pcm[i] = sample < 0 ? sample * 32768 : sample * 32767;
    }
    console.log("PCM size:", pcm.buffer.byteLength)
    return pcm.buffer;
}

// Each chunk: [type:1][seq:2][total:2][payload:N]
function buildChunk(type, seqNum, totalChunks, payload) {
    const buffer = new ArrayBuffer(5 + payload.length);
    const view = new DataView(buffer);
    view.setUint8(0, type);        // 0=motor, 1=audio
    view.setUint16(1, seqNum, true);
    view.setUint16(3, totalChunks, true);
    new Uint8Array(buffer, 5).set(payload);
    return buffer;
}

async function sendBinary(packet, characteristic) {
  const bytes = new Uint8Array(packet);
  const chunkSize = 480; //195;
  const totalChunks = Math.ceil(bytes.length / chunkSize);
  // const BATCH_SIZE = 10;

  console.log(`Sending ${totalChunks} chunks...`);
  const startTime = Date.now();

  for (let i = 0; i < totalChunks; i++) {
    const slice = bytes.slice(i * chunkSize, (i + 1) * chunkSize);
    const chunk = buildChunk(0, i, totalChunks, slice);
    // This sends without waiting for an "OK" from the fish every time --> much faster.
    await characteristic.writeValueWithoutResponse(chunk);
    
    // Tiny delay every 20 chunks to prevent overflowing the ESP32's buffer
    if (i % 20 === 0) await new Promise(r => setTimeout(r, 10));
  }

  const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
  console.log(`Send complete in ${elapsed}s`);
}