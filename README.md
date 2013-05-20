# soundspace
  
  soundspace is a surround sound player which can be controlled using a simple json based protocol.
  
# usage
 
  add new file as sound source:
  
 
 
  plays all added sounds: 
  
    { "cmd":"play", "ids":true }
    

  rotate sound source during 1 minute with speed of


  
  fade sound to level:
  

# dependencies

  to build and use soundspace the following libraries are required: boost, libevent, jsoncpp and openal.
  
  install dependencies on debian/ubuntu:
  
    sudo apt-get install libopenal-dev libboost-dev libevent-dev libjsoncpp-dev

# building

  make

# copying

  this software is release under the AGPL version 3. A copy of the license can be found in the file 'LICENSE'.
