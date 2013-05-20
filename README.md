# soundspace
  
  soundspace is a surround sound player which can be controlled using a simple json based protocol.

# dependencies

  to build and use soundspace the following libraries are required: boost, libevent, jsoncpp and openal.

# building

  make

# configuration

  soundspace will use the default openal device. on most linux machines this usually is the software implementation called 'alsoft'. therefore, in order to configure the sound device that is being used and to adjust the speaker settings, alsoft has to be configured. the easiest way to do that is to use 'alsoft-conf', which is available on most distributions.

# copying

  this software is release under the AGPL version 3. A copy of the license can be found in the file 'LICENSE'.
