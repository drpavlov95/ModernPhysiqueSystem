class MeshWindow extends MovableWindow
{
   var _parent;
   var meshList;
   var bLoadedAssets = false;
   static var colors = [16777215,16711680,255,65280,16711935,16776960,65535,7992050,15041651,15102938,5757238,13384960,5387212,13407334,12517405,12107568,9175166,4615564,2650880,3765081,4536691,6696448,327782,6708762,6697794,5845805,4980747,4198459,3351565,2110221,857651,1717039];
   function MeshWindow()
   {
      super();
      this.meshList.disableSelection = this.meshList.disableInput = true;
   }
   function onLoad()
   {
      super.onLoad();
      this.meshList.listEnumeration = new skyui.components.list.BasicEnumeration(this.meshList.entryList);
      this.meshList.addEventListener("itemPressVisibility",this,"onItemPressVisibility");
      this.meshList.addEventListener("itemPressWireframe",this,"onItemPressWireframe");
      this.meshList.addEventListener("itemPressLock",this,"onItemPressLock");
      this.meshList.addEventListener("itemPressAux",this,"onItemPressAux");
      this.meshList.addEventListener("selectionChange",this,"onSelectionChange");
      this.meshList.addEventListener("statusRollOver",this,"onStatusChange");
      this.meshList.requestInvalidate();
   }
   function loadAssets()
   {
      if(this.bLoadedAssets)
      {
         return true;
      }
      this.meshList.disableSelection = this.meshList.disableInput = false;
      this.meshList.entryList = _global.skse.plugins.CharGen.GetMeshes();
      this.meshList.listEnumeration = new skyui.components.list.BasicEnumeration(this.meshList.entryList);
      this.meshList.requestInvalidate();
      this.bLoadedAssets = true;
      return true;
   }
   function unloadAssets()
   {
      if(!this.bLoadedAssets)
      {
         return false;
      }
      this.meshList.disableSelection = this.meshList.disableInput = true;
      this.meshList.entryList.splice(0,this.meshList.entryList.length);
      this.meshList.requestInvalidate();
      this.bLoadedAssets = false;
      return true;
   }
   function GetInternalMeshes()
   {
      return this.meshList.entryList;
   }
   function onItemPressVisibility(event)
   {
      var _loc3_ = this.meshList.entryList[event.index];
      if(_loc3_)
      {
         _loc3_.visible = !_loc3_.visible;
         if(_global.skse.plugins.CharGen.SetMeshData)
         {
            _global.skse.plugins.CharGen.SetMeshData(_loc3_.meshIndex,_loc3_);
         }
         this.meshList.requestUpdate();
      }
   }
   function onItemPressWireframe(event)
   {
      var _loc3_ = this.meshList.entryList[event.index];
      if(_loc3_)
      {
         _loc3_.wireframe = !_loc3_.wireframe;
         if(_global.skse.plugins.CharGen.SetMeshData)
         {
            _global.skse.plugins.CharGen.SetMeshData(_loc3_.meshIndex,_loc3_);
         }
         this.meshList.requestUpdate();
      }
   }
   function onItemPressLock(event)
   {
      var _loc3_ = this.meshList.entryList[event.index];
      if(_loc3_)
      {
         _loc3_.locked = !_loc3_.locked;
         if(_global.skse.plugins.CharGen.SetMeshData)
         {
            _global.skse.plugins.CharGen.SetMeshData(_loc3_.meshIndex,_loc3_);
         }
         this.meshList.requestUpdate();
      }
   }
   function forceInverseLock(a_index)
   {
      var _loc4_ = 0;
      var _loc3_;
      while(_loc4_ < this.meshList.entryList.length)
      {
         _loc3_ = this.meshList.entryList[_loc4_];
         if(_loc3_.meshIndex != a_index)
         {
            if(_loc3_.locked == false)
            {
               _loc3_.locked = true;
               _global.skse.plugins.CharGen.SetMeshData(_loc3_.meshIndex,_loc3_);
            }
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function onItemPressAux(event)
   {
      var _loc3_ = this.meshList.entryList[event.index];
      var _loc4_;
      if(_loc3_)
      {
         _loc4_ = Math.floor(Math.random() * MeshWindow.colors.length);
         _loc3_.wfColor = 0xFF000000 | MeshWindow.colors[_loc4_] >>> 0;
         if(_global.skse.plugins.CharGen.SetMeshData)
         {
            _global.skse.plugins.CharGen.SetMeshData(_loc3_.meshIndex,_loc3_);
         }
         this.meshList.requestUpdate();
      }
   }
   function onSelectionChange(event)
   {
      var _loc2_ = this.meshList.entryList[event.index];
      this.meshList.listState.selectedEntry = _loc2_;
      this.meshList.requestUpdate();
   }
   function onStatusChange(event)
   {
      var _loc3_ = this.meshList.entryList[event.index];
      switch(event.status)
      {
         case 0:
            this._parent._parent.setStatusText("$Toggle Visibility");
            break;
         case 1:
            this._parent._parent.setStatusText("$Toggle Wireframe");
            break;
         case 2:
            this._parent._parent.setStatusText("$Toggle Editability");
            break;
         case 3:
            this._parent._parent.setStatusText("$Cycle Wireframe Color");
         default:
            return;
      }
   }
}
