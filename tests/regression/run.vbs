Option Explicit

' This file is part of OpenTTD.
' OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
' OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
' See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.

Dim FSO
Set FSO = CreateObject("Scripting.FileSystemObject")

Function GetTestList()
	Dim retests, i, tests, dir
	Set retests = New RegExp
	Set GetTestList = CreateObject("Scripting.Dictionary")

	retests.Pattern = "tests/regression/*.sav"
	retests.Global = True
	For i = 0 To WScript.Arguments.Count - 1
		Dim test
		test = WScript.Arguments.Item(i)
		If FSO.FileExists(test) Then
			retests.Pattern = test
			Exit For
		End If
	Next

	For Each file In FSO.GetFolder("tests/regression/").Files
		Dim name
		name = "tests/regression/" & file.Name
		If retests.Test(name) Then
			GetTestList.Add name, name
		End If
	Next
End Function

Function GetParams()
	GetParams = "-snull -mnull -vnull:ticks=10000"
	If WScript.Arguments.Count = 0 Then Exit Function
	If WScript.Arguments.Item(0) <> "-r" Then Exit Function
	GetParams = ""
End Function

Sub RunTest(sav, params, ret)
	Dim WshShell, oExec, sav, command
	Set WshShell = CreateObject("WScript.Shell")

	SavName = FSO.GetBaseName(sav)
	TestFolder = "tests/regression/" & SavName
	cfg = TestFolder & "/default.cfg"
	If Not FSO.FileExists(cfg) Then
		FSO.CopyFile "tests/regression/default.cfg", cfg
	End If

	command = ".\bin\openttd -x -c " & cfg & " " & params & " -g " & sav & " -d sl=1"
	' 2>&1 must be after >tmp.regression, else stderr is not redirected to the file
	ret = WshShell.Run "cmd /c " & command, 0, True

	If ret = 0 Then
		FSO.DeleteFolder TestFolder
	End If
End Sub

On Error Resume Next
WScript.StdOut.WriteLine ""
If Err.Number <> 0 Then
	WScript.Echo "This script must be started with cscript."
	WScript.Quit 1
End If
On Error Goto 0

If Not FSO.FileExists("tests/regression/run.vbs") Then
	WScript.Echo "Make sure you are in the root of OpenTTD checkout before starting this script."
	WScript.Quit 1
End If

Dim params, test, ret
params = GetParams()
ret = 0

For Each test in GetTestList()
	WScript.StdOut.WriteLine "Running " & test & "... "
	RunTest(test, params, ret)
	if ret <> 0 Then Exit For
Next

WScript.Quit ret
