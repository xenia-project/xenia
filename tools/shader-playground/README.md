This requires XNA Game Studio 3.1 to be installed (not just the redist):
https://www.microsoft.com/en-us/download/details.aspx?id=39

It's not really compatible with modern VS', but you can open the downloaded
`XNAGS31_setup.exe` with 7zip and run the included `redists.msi` directly.

If installed correctly you should have this file:
`C:\Program Files (x86)\Microsoft XNA\XNA Game Studio\v3.1\References\Windows\x86\Microsoft.Xna.Framework.dll`

XNA is only compatible with 32-bit x86 .NET, so ensure Visual Studio is set to
target that (not `Any CPU`).
