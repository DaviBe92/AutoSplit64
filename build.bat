call .venv\Scripts\activate.bat

pyinstaller ^
--noconfirm ^
--name "AutoSplit64plus" ^
--splash "resources\gui\icons\as64plus.png" ^
--icon "resources\gui\icons\as64plus.ico" ^
--noconsole ^
--clean ^
--noupx ^
--contents-directory "libraries" ^
AutoSplit64.py

xcopy /E /I /Y logic dist\AutoSplit64plus\logic
xcopy /E /I /Y resources dist\AutoSplit64plus\resources
xcopy /E /I /Y routes dist\AutoSplit64plus\routes
xcopy /E /I /Y templates dist\AutoSplit64plus\templates
xcopy /Y defaults.ini dist\AutoSplit64plus\
xcopy /Y obs-plugin\build_x64\RelWithDebInfo\autosplit64plus-framegrabber.dll dist\AutoSplit64plus\obs-plugin\
