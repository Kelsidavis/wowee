#!/bin/bash
# Test if splash sounds load properly

echo "Testing splash sound files..."
echo ""

for file in \
  "Sound\\Character\\Footsteps\\EnterWaterSplash\\EnterWaterSmallA.wav" \
  "Sound\\Character\\Footsteps\\EnterWaterSplash\\EnterWaterMediumA.wav" \
  "Sound\\Character\\Footsteps\\EnterWaterSplash\\EnterWaterGiantA.wav" \
  "Sound\\Character\\Footsteps\\WaterSplash\\FootStepsMediumWaterA.wav"
do
  echo -n "Checking: $file ... "
  if ./list_mpq Data/common.MPQ "$file" 2>/dev/null | grep -q "wav"; then
    echo "✓ EXISTS"
  else
    echo "✗ NOT FOUND"
  fi
done

echo ""
echo "Now run the game and check for:"
echo "  Activity SFX loaded: jump=X splash=8 swimLoop=X"
echo ""
echo "If splash=0, the files aren't being loaded by ActivitySoundManager"
