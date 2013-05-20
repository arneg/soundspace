# Soundspace
  
  Soundspace is a surround sound player which can be controlled using a simple json based protocol.
  
# Installing

  To build and use soundspace the following libraries are required: boost, libevent, jsoncpp and openal.
  
  Install dependencies on Debian/Ubuntu:
  
    sudo apt-get install libopenal-dev libboost-dev libevent-dev libjsoncpp-dev

  Checkout from github and compile:
    
    make
    
  Install system wide
  
    make install

  Run
  
    spacesound
    
# Usage

  Soundspace accepts json formatted commands on STDIN.
  
# Commands
 
  Add new file as sound source:
  
    {"cmd":"add_source", "file": "fullpath/filename.wav", "gain":1, "position":[0,0,-1], "loop":true}
 
  Play all loaded sounds: 
  
    {"cmd":"play", "ids":true}
    
  Plays one sound: 
  
    {"cmd":"play", "ids":"fullpath/filename.wav"}
    
  Rotate sound source during 1 minute with speed of 0.2

    { "cmd":"rotate", "speed":0.2, "time":60, "ids":true }
  
  Fade sound out in 5 seconds:
  
    {"cmd":"fade","time":5, "gain":0,"ids":"fullpath/filename.wav"}
  
  Stop audio:
    
    {"cmd":"stop_audio","ids":"fullpath/filename.wav"}

  Remove audio source:
  
    {"cmd":"remove_source","ids":"fullpath/filename.wav"}

# Tips

  Three ways to specify target sounds
  
  Affect all loaded sounds:
    
    ids: true
      
  Affect multiple loaded sounds:
    
    ids: ["fullpath/filename1.wav","fullpath/filename2.wav","fullpath/filename3.wav"]
      
  Affect one loaded sound:
    
    ids: "fullpath/filename.wav"
      
# Use through http

  To use Spacesound from web tools (HTML5, Canvas, etc) it is possible to forward commands through a simple web server.
  
  Run Spacesound as a socket and write with server side script to that socket:

    need a socat or nc command here
  
# License

  This software is release under the AGPL version 3. A copy of the license can be found in the file 'LICENSE'.
