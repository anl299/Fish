/* var wavesurfer = WaveSurfer.create({
    container: "#audiowave",
    waveColor: "#5df9de",
    progressColor: "#1e594f",
    height: 150,
    responsive: true,
    hideScrollbar: true,
    cursorColor: "#5df9de",
    cursorWidth: 2,
    barWidth: 5,
    barGap: 1.5,
    skipLength: 5
});

wavesurfer.load("vibe-track.mp3");

$(".btn-toggle-pause").on("click", function() {
    wavesurfer.playPause();
});

$(".btn-backward").on("click", function() {
    wavesurfer.skipBackward();
});

$(".btn-forward").on("click", function() {
    wavesurfer.skipForward();
});

$(".btn-toggle-mute").on("click", function() {
    wavesurfer.toggleMute();
});

$(".btn-stop").on("click", function() {
    wavesurfer.stop();
}); */


// import WaveSurfer from 'wavesurfer.js'
import WaveSurfer from 'https://unpkg.com/wavesurfer.js@7/dist/wavesurfer.esm.js'
import RegionsPlugin from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/regions.esm.js'
import Timeline from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/timeline.esm.js'
import Record from 'https://unpkg.com/wavesurfer.js@7/dist/plugins/record.esm.js'
// import Multitrack from 'https://unpkg.com/wavesurfer-multitrack/dist/multitrack.esm.js'
// import Multitrack from 'https://cdn.jsdelivr.net/npm/wavesurfer-multitrack/dist/multitrack.esm.js'


/////         Store UI Elements         /////
const fileInput = document.getElementById('fileInput')
const recordBtn = document.getElementById('record')

const questionBtn = document.getElementById('questionBtn')

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

/////         Global Variables         /////
const minMouthTime = 0.25 // seconds
const minHeadTime = 0.5 // seconds
const minTailTime = 0.5 // seconds

const regions = RegionsPlugin.create()
let audioWave_full
let audioWave_mouth
let audioWave_body
let audioFile = null
let editMode = "draw" //  "draw" <--> "erase"
let bodyMode = "head" //  "head" <--> "tail"


document.addEventListener("DOMContentLoaded", () => {
  initializeAudio('* need to add audio file here *'); // *** need to add audio file here ***
})
drawBtn.addEventListener("click", setEditMode)
eraseBtn.addEventListener("click", setEditMode)
headBodyChoice.addEventListener("click", swapBodyMode)


function initializeAudio(audiofile){
  let drawBGcolor = 'rgb(50, 125, 255)'
  if (audioFile == null){
    drawBGcolor = 'rgb(100, 100, 100)'
  }
  
    audioWave_full = WaveSurfer.create({
      container: document.getElementById('audioOverview'),
      waveColor: 'rgb(200, 0, 200)',
      progressColor: 'rgb(100, 0, 100)',
      url: '/test/sfx_grunt-birthday-party.mp3',
      minPxPerSec: 100,
      dragToSeek: true
    })

    audioWave_mouth = WaveSurfer.create({
      container: document.getElementById('mouthControl'),
      waveColor: drawBGcolor,
      url: '/test/sfx_grunt-birthday-party.mp3',
      minPxPerSec: 100,
      dragToSeek: false,
      plugins: [regions]
    })
    audioWave_body = WaveSurfer.create({
      container: document.getElementById('bodyControl'),
      waveColor: drawBGcolor,
      url: '/test/sfx_grunt-birthday-party.mp3',
      minPxPerSec: 100,
      dragToSeek: false,
      // plugins: [regions]
    })

  // Update the zoom level on slider change
  audioWave_full.once('decode', () => {
    zoomSlider.addEventListener('input', (e) => {
      const minPxPerSec = e.target.valueAsNumber
      audioWave_full.zoom(minPxPerSec)
    })

  // More controls
  const playButton = document.querySelector('#play')
  const forwardButton = document.querySelector('#forward')
  const backButton = document.querySelector('#backward')
    document.querySelectorAll('input[type="checkbox"]').forEach((input) => {
      input.onchange = (e) => {
        audioWave_full.setOptions({
          [input.value]: e.target.checked,
        })
      }
    })
    playButton.onclick = () => audioWave_full.playPause()
    forwardButton.onclick = () => audioWave_full.skip(5)
    backButton.onclick = () => audioWave_full.skip(-5)
  })
}


// // **********  **********  ********** //
// regions.enableDragSelection({
//   color: 'rgba(255, 0, 0, 0.6)',
// })

// regions.on('region-updated', (region) => {
//   console.log('Updated region', region)
// })

// // Loop a region on click
// let loop = true
// // Toggle looping with a checkbox
// // document.querySelector('input[type="checkbox"]').onclick = (e) => {
// //   loop = e.target.checked
// // }

// {
//   let activeRegion = null
//   regions.on('region-in', (region) => {
//     console.log('region-in', region)
//     activeRegion = region
//   })
//   regions.on('region-out', (region) => {
//     console.log('region-out', region)
//     if (activeRegion === region) {
//       if (loop) {
//         region.play()
//       } else {
//         activeRegion = null
//       }
//     }
//   })
//   regions.on('region-clicked', (region, e) => {
//     e.stopPropagation() // prevent triggering a click on the waveform
//     activeRegion = region
//     region.play(true)
//     region.setOptions({ color: randomColor() })
//   })
//   // Reset the active region when the user clicks anywhere in the waveform
//   // ws.on('interaction', () => {
//   //   activeRegion = null
//   // })
// }
// // **********  **********  ********** //


/////         LOADING NEW SOUND FILE          /////
fileInput.addEventListener('change', function(e){
    var file = this.files[0];

    if (file) {
        var reader = new FileReader();
        
        reader.onload = function (evt) {
            // Create a Blob providing as first argument a typed array with the file buffer
            var blob = new window.Blob([new Uint8Array(evt.target.result)]);

            // Load the blob into Wavesurfer
            audioWave_full.loadBlob(blob);
            audioWave_mouth.loadBlob(blob);
            audioWave_body.loadBlob(blob);
        };

        reader.onerror = function (evt) {
            console.error("An error ocurred reading the file: ", evt);
        };

        // Read File as an ArrayBuffer
        reader.readAsArrayBuffer(file);
    }
}, false);


/////         SWAPING HEAD/TAIL EDIT MODE         /////
function swapBodyMode(){
  if (bodyMode == "head"){ // Change to TAIL
    bodyMode = "tail"
    headPic.classList.add("hidden")
    tailPic.classList.remove("hidden")

    headControlTitle.classList.add("inactive")
    tailControlTitle.classList.remove("inactive")

  } else{       //  Change to HEAD
    bodyMode = "head"
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
    // Make Image visibly selected
  } else {
    editMode = "erase"
    // Make Image visibly selected
  }
}


