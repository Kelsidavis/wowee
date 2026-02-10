#!/bin/bash
# Test script for ambient audio debugging

echo "=== Testing Ambient Audio System ==="
echo ""
echo "Running game for 60 seconds and capturing logs..."
echo ""

timeout 60 build/bin/wowee 2>&1 | tee /tmp/wowee_ambient_test.log

echo ""
echo "=== Analysis ==="
echo ""

echo "1. AmbientSoundManager Initialization:"
grep -i "AmbientSoundManager" /tmp/wowee_ambient_test.log | head -20
echo ""

echo "2. Fire Emitters Detected:"
grep -i "fire emitter" /tmp/wowee_ambient_test.log
echo ""

echo "3. Water Emitters Registered:"
grep -i "water.*emitter" /tmp/wowee_ambient_test.log | head -10
echo ""

echo "4. Sample WMO Doodads Loaded:"
grep "WMO doodad:" /tmp/wowee_ambient_test.log | head -20
echo ""

echo "5. Total Ambient Emitters:"
grep "Registered.*ambient" /tmp/wowee_ambient_test.log
echo ""

echo "Full log saved to: /tmp/wowee_ambient_test.log"
echo "Use 'grep <keyword> /tmp/wowee_ambient_test.log' to search for specific issues"
