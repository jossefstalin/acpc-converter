/* Air Conflicts Workshop 0.1 - MaxScript adapter, 2015+ API target.
   Requires Python 3.10+ or Blender 3.6+ as the local format bridge runtime.
   Static import/export; map channels 1 (color) and 2 (light) are retained.
   This adapter has not been executed inside Autodesk 3ds Max in this build.
*/
global ACWorkshopDir = getFilenamePath (getSourceFileName())
global ACWorkshopRoot = pathConfig.appendPath ACWorkshopDir ".."
try(destroyDialog ACWorkshopDialog)catch()
rollout ACWorkshopDialog "Air Conflicts Workshop 0.1" width:470 height:300
(
    editText runtime "Python / Blender EXE:" text:"python.exe" fieldWidth:315
    button browse "Choose runtime" width:440
    editText target "Game plane folder:" text:"my_plane" fieldWidth:325
    button openModel "Open BMF / MSH (UV1 + UV2)" width:440
    button saveBMF "Export selected static body BMF" width:440
    button saveMSH "Export selected cockpit MSH + MTL" width:440
    label note "OBJ: import normally, then select the mesh. BMF requires one atlas." width:445
    label status "Max adapter: experimental; Blender path is also supported." width:445
    fn quoted s = ("\"" + s + "\"")
    fn runBridge args =
    (
        local psi=dotNetObject "System.Diagnostics.ProcessStartInfo"
        psi.FileName=runtime.text
        local script=pathConfig.appendPath ACWorkshopRoot "max_bridge.py"
        if matchPattern (toLower runtime.text) pattern:"*blender.exe" then
            psi.Arguments="--background --factory-startup --python "+(quoted script)+" -- "+args
        else psi.Arguments=(quoted script)+" "+args
        psi.UseShellExecute=false
        psi.CreateNoWindow=true
        psi.RedirectStandardError=true
        local proc=dotNetObject "System.Diagnostics.Process"
        proc.StartInfo=psi
        proc.Start()
        local err=proc.StandardError.ReadToEnd()
        proc.WaitForExit()
        if proc.ExitCode != 0 then throw err
        true
    )
    fn triple p = ((p.x as string)+" "+(p.y as string)+" "+(p.z as string))
    fn pair p = ((p.x as string)+" "+(p.y as string))
    fn exchange obj filename =
    (
        local m=snapshotAsMesh obj
        if not (meshop.getMapSupport m 1) then throw "The mesh has no color UV channel."
        local settings=dotNetObject "System.Xml.XmlWriterSettings"
        settings.Indent=false
        local w=(dotNetClass "System.Xml.XmlWriter").Create filename settings
        w.WriteStartDocument()
        w.WriteStartElement "ac_mesh"
        local mats=if classof obj.material==MultiMaterial then obj.material.materialList else #(obj.material)
        for i=1 to mats.count do
        (
            w.WriteStartElement "material"
            w.WriteAttributeString "name" (if mats[i]!=undefined then mats[i].name else ("material_"+(i as string)))
            local texture=""
            if mats[i]!=undefined and isProperty mats[i] #diffuseMap and mats[i].diffuseMap!=undefined and isProperty mats[i].diffuseMap #filename do texture=mats[i].diffuseMap.filename
            w.WriteAttributeString "diffuse" texture
            w.WriteEndElement()
        )
        for f=1 to m.numFaces do
        (
            local face=getFace m f
            local uvface=meshop.getMapFace m 1 f
            local lightface=if meshop.getMapSupport m 2 then meshop.getMapFace m 2 f else uvface
            w.WriteStartElement "face"
            local mid=amin (mats.count-1) (amax 0 ((getFaceMatID m f)-1))
            w.WriteAttributeString "material" (mid as string)
            for c=1 to 3 do
            (
                local v=face[c] as integer
                local p=(getVert m v)*obj.objectTransform
                local n=normalize ((getNormal m v)*(obj.objectTransform.rotation as matrix3))
                local uv=meshop.getMapVert m 1 (uvface[c] as integer)
                local light=if meshop.getMapSupport m 2 then meshop.getMapVert m 2 (lightface[c] as integer) else [0.5,0.5,0]
                w.WriteStartElement "corner"
                w.WriteAttributeString "p" (triple p)
                w.WriteAttributeString "n" (triple n)
                w.WriteAttributeString "uv0" (pair uv)
                w.WriteAttributeString "uv1" (pair light)
                w.WriteEndElement()
            )
            w.WriteEndElement()
        )
        w.WriteEndElement()
        w.WriteEndDocument()
        w.Close()
        free m
    )
    fn saveSelected kind =
    (
        if selection.count!=1 then throw "Select exactly one mesh object."
        local output=getSaveFileName caption:("Export "+kind) types:(kind+" (*."+kind+")|*."+kind+"|")
        if output!=undefined do
        (
            local xml=(getDir #temp)+"\\ac_workshop_"+(timestamp() as string)+".xml"
            exchange selection[1] xml
            runBridge ("from-max "+(quoted xml)+" "+(quoted output)+" --kind "+kind+" --target "+(quoted target.text))
            deleteFile xml
            status.text="Export complete: "+output
        )
    )
    on browse pressed do
    (
        local p=getOpenFileName caption:"Choose python.exe or blender.exe" types:"Executable (*.exe)|*.exe|"
        if p!=undefined do runtime.text=p
    )
    on openModel pressed do try
    (
        local p=getOpenFileName caption:"Open Air Conflicts model" types:"Air Conflicts (*.bmf;*.msh)|*.bmf;*.msh|"
        if p!=undefined do
        (
            local generated=(getDir #temp)+"\\ac_workshop_"+(timestamp() as string)+".ms"
            runBridge ("to-max "+(quoted p)+" "+(quoted generated))
            clearSelection()
            fileIn generated quiet:true
            deleteFile generated
            status.text="Opened: "+p
        )
    ) catch(messageBox (getCurrentException()) title:"Air Conflicts")
    on saveBMF pressed do try(saveSelected "bmf")catch(messageBox (getCurrentException()) title:"Air Conflicts")
    on saveMSH pressed do try(saveSelected "msh")catch(messageBox (getCurrentException()) title:"Air Conflicts")
)
createDialog ACWorkshopDialog
