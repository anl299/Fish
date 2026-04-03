// import WaveSurfer from 'wavesurfer.js'
import WaveSurfer from 'https://unpkg.com/wavesurfer.js@7/dist/wavesurfer.esm.js'
import RegionsPlugin from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/regions.esm.js'
import Timeline from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/timeline.esm.js'
import Record from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/record.esm.js'
// import Multitrack from 'https://unpkg.com/wavesurfer-multitrack/dist/multitrack.esm.js'
// import Multitrack from 'https://cdn.jsdelivr.net/npm/wavesurfer-multitrack/dist/multitrack.esm.js'


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
const minMouthTime = 0.25 // seconds
const minBodyTime = 0.5 // seconds
const minHeadTime = 1.5 // seconds
const minTailTime = 0.5 // seconds


let audioWave_full = null
let audioWave_mouth = null
let regions_mouth = null
let audioWave_body = null
let regions_body = null
let recordPlugin = null
// let audioFile = null
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
  regions_mouth = RegionsPlugin.create()
  regions_body = RegionsPlugin.create()

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
    color: 'rgb(225, 225, 50)', // bodyDrawColor,
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
    region.setOptions({
      drag: true,
      resize: true
    })

    const regions = Object.values(regions_mouth.getRegions())
    if (overlaps(region, regions)) {
      region.remove()
    }

    // Add downward slope //
  })

  regions_body.on('region-created', (region) => {
    region.setOptions({
      drag: true,
      resize: true
    })

    region.setOptions({ color: bodyDrawColor });
    const regions = Object.values(regions_body.getRegions())
    if (overlaps(region, regions)) {
      region.remove()
    }

    // Add downward slope //
  })

  //  ******
  // regions_mouth.on('region-update-end', (region) => {
  //   const regions = Object.values(regions_mouth.getRegions())
  //   for (const other of regions) {
  //     if (other === region) continue
  //     if (region.start < other.end && region.end > other.start) {
  //         region.setOptions({
  //             start: other.end
  //         })
  //     }
  //   }
  // })
  function overlaps(region, regions) {
    return regions.some(r =>
      r !== region &&
      region.start < r.end &&
      region.end > r.start
    )
  }
  //  ******


  /////         LOADING / RECORDING NEW AUDIO         /////
  fileInput.addEventListener('change', function(e){ // file input: when a file is selected, load it into the master
    const file = this.files && this.files[0]
    if (!file) return
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

  //***     Load audio into main player ONLY. Other players use same processed data     ***//
  // if (audioWave_mouth){
  //   audioWave_mouth.loadBlob(blob)
  // }
  // if (audioWave_body){
  //   audioWave_body.loadBlob(blob)
  // }
  //  ******
}


/////         SWAPING HEAD/TAIL EDIT MODE         /////
function swapBodyMode(){
  if (bodyMode == "head"){ // Change to TAIL
    bodyMode = "tail"
    // regions_body.setOptions({ color: 'rgba(50,255,50,0.25)' });
    bodyDrawColor = 'rgba(255,50,50,0.25)'
    headPic.classList.add("hidden")
    tailPic.classList.remove("hidden")
    headControlTitle.classList.add("inactive")
    tailControlTitle.classList.remove("inactive")

  } else{       //  Change to HEAD
    bodyMode = "head"
    // regions_body.setOptions({ color: 'rgba(255,50,50,0.25)' });
    bodyDrawColor = 'rgba(50,255,50,0.25)'
    headPic.classList.remove("hidden")
    tailPic.classList.add("hidden")
    headControlTitle.classList.remove("inactive")
    tailControlTitle.classList.add("inactive")
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

