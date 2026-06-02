@echo off
REM compile_hdr_shaders.bat — Compile HDR PQ shaders with Qt Shader Tools (qsb)
REM Run from: mfplayer/ui/qml/shaders/
REM qsb path: C:\Qt\6.11.1\msvc2022_64\bin\qsb.exe

set QSB=C:\Qt\6.11.1\msvc2022_64\bin\qsb.exe

echo === Compiling hdr_pq.vert ===
%QSB% --glsl "440" --hlsl 50 --msl 12 -o hdr_pq.vert.qsb hdr_pq.vert

echo === Compiling hdr_pq.frag ===
%QSB% --glsl "440" --hlsl 50 --msl 12 -o hdr_pq.frag.qsb hdr_pq.frag

echo === Done ===
dir *.qsb
