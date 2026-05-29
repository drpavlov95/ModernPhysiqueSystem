class BrushWindow extends MovableWindow
{
   var _typeFilter;
   var brushList;
   var brushSlidingList;
   var brushes;
   var categoryList;
   var dispatchEvent;
   var entryObject;
   var currentBrush = -1;
   static var BRUSH_TYPE_NONE = -1;
   static var BRUSH_TYPE_MASK_ADD = 0;
   static var BRUSH_TYPE_MASK_SUB = 1;
   static var BRUSH_TYPE_INFLATE = 2;
   static var BRUSH_TYPE_DEFLATE = 3;
   static var BRUSH_TYPE_MOVE = 4;
   static var BRUSH_TYPE_SMOOTH = 5;
   var _allowBrushChange = true;
   var bLoadedAssets = false;
   function BrushWindow()
   {
      super();
      this.categoryList = this.brushSlidingList.categoryList;
      this._typeFilter = new CategoryFilter();
      this.categoryList.disableInput = this.brushList.disableInput = false;
   }
   function onLoad()
   {
      super.onLoad();
      this._typeFilter.addEventListener("filterChange",this,"onFilterChange");
      this.categoryList.addEventListener("selectionChange",this,"onCategoryChange");
      this.categoryList.listEnumeration = new skyui.components.list.BasicEnumeration(this.categoryList.entryList);
      this.categoryList.requestInvalidate();
      var _loc3_ = new skyui.components.list.FilteredEnumeration(this.brushList.entryList);
      _loc3_.addFilter(this._typeFilter);
      this.brushList.listEnumeration = _loc3_;
      this.brushList.requestInvalidate();
   }
   function InitExtensions()
   {
   }
   function set bAllowBrushChange(a_change)
   {
      this._allowBrushChange = a_change;
      this.categoryList.disableInput = this.brushList.disableInput = !this._allowBrushChange;
   }
   function loadAssets()
   {
      if(this.bLoadedAssets)
      {
         return true;
      }
      this.brushes = _global.skse.plugins.CharGen.GetBrushes();
      var _loc3_ = 0;
      var _loc4_;
      var _loc6_;
      var _loc7_;
      var _loc8_;
      var _loc9_;
      while(_loc3_ < this.brushes.length)
      {
         _loc4_ = "brush_" + this.brushes[_loc3_].type;
         this.categoryList.entryList.push({text:this.getBrushName(this.brushes[_loc3_].type),flag:0,textFilter:_loc4_,bDontHide:true,filterFlag:1,enabled:true});
         _loc6_ = {text:"$Radius",sliderMin:this.brushes[_loc3_].radiusMin,sliderMax:this.brushes[_loc3_].radiusMax,filterFlag:0,textFilters:[_loc4_],interval:this.brushes[_loc3_].radiusInterval,position:this.brushes[_loc3_].radius,brush:this.brushes[_loc3_],enabled:true};
         _loc6_.internalCallback = function()
         {
            var _loc3_ = this.entryObject.brush;
            _loc3_.radius = this.entryObject.position;
            if(_global.skse.plugins.CharGen.SetBrushData)
            {
               _global.skse.plugins.CharGen.SetBrushData(_loc3_.type,_loc3_);
            }
         };
         this.brushList.entryList.push(_loc6_);
         if(this.brushes[_loc3_].strength != 0)
         {
            _loc7_ = {text:"$Strength",sliderMin:this.brushes[_loc3_].strengthMin,sliderMax:this.brushes[_loc3_].strengthMax,filterFlag:0,textFilters:[_loc4_],interval:this.brushes[_loc3_].strengthInterval,position:this.brushes[_loc3_].strength,brush:this.brushes[_loc3_],enabled:true};
            _loc7_.internalCallback = function()
            {
               var _loc3_ = this.entryObject.brush;
               _loc3_.strength = this.entryObject.position;
               if(_global.skse.plugins.CharGen.SetBrushData)
               {
                  _global.skse.plugins.CharGen.SetBrushData(_loc3_.type,_loc3_);
               }
            };
            this.brushList.entryList.push(_loc7_);
         }
         if(this.brushes[_loc3_].falloff != 0)
         {
            _loc8_ = {text:"$Falloff",sliderMin:this.brushes[_loc3_].falloffMin,sliderMax:this.brushes[_loc3_].falloffMax,filterFlag:0,textFilters:[_loc4_],interval:this.brushes[_loc3_].falloffInterval,position:this.brushes[_loc3_].falloff,brush:this.brushes[_loc3_],enabled:true};
            _loc8_.internalCallback = function()
            {
               var _loc3_ = this.entryObject.brush;
               _loc3_.falloff = this.entryObject.position;
               if(_global.skse.plugins.CharGen.SetBrushData)
               {
                  _global.skse.plugins.CharGen.SetBrushData(_loc3_.type,_loc3_);
               }
            };
            this.brushList.entryList.push(_loc8_);
         }
         _loc9_ = {text:"$Mirror",sliderMin:0,sliderMax:1,filterFlag:0,textFilters:[_loc4_],interval:1,position:this.brushes[_loc3_].mirror,brush:this.brushes[_loc3_],enabled:true};
         _loc9_.internalCallback = function()
         {
            var _loc3_ = this.entryObject.brush;
            _loc3_.mirror = this.entryObject.position;
            if(_global.skse.plugins.CharGen.SetBrushData)
            {
               _global.skse.plugins.CharGen.SetBrushData(_loc3_.type,_loc3_);
            }
         };
         this.brushList.entryList.push(_loc9_);
         _loc3_ = _loc3_ + 1;
      }
      var _loc21_ = _global.skse.plugins.CharGen.GetCurrentBrush();
      this.currentBrush = this.getBrushIndex(_loc21_);
      this.categoryList.InvalidateData();
      this.categoryList.onItemPress(this.currentBrush,0);
      this.brushList.requestInvalidate();
      this.bLoadedAssets = true;
      return true;
   }
   function unloadAssets()
   {
      if(!this.bLoadedAssets)
      {
         return false;
      }
      this.categoryList.entryList.splice(0,this.categoryList.entryList.length);
      this.brushList.entryList.splice(0,this.brushList.entryList.length);
      this.categoryList.requestInvalidate();
      this.brushList.requestInvalidate();
      this.brushes.splice(0,this.brushes.length);
      this.brushes = null;
      this.currentBrush = -1;
      this.bLoadedAssets = false;
      return true;
   }
   function handleInput(details, pathToFocus)
   {
      if(this.categoryList.handleInput(details,pathToFocus))
      {
         return true;
      }
      return this.brushList.handleInput(details,pathToFocus);
   }
   function onFilterChange(event)
   {
      this.brushList.requestInvalidate();
   }
   function onCategoryChange(event)
   {
      var _loc3_;
      if(this.categoryList.selectedEntry != undefined)
      {
         _loc3_ = this.brushes[event.index];
         if(_loc3_)
         {
            this.dispatchEvent({type:"changeBrush",id:_loc3_.type,brushName:this.getBrushName(_loc3_.type)});
            if(_global.skse.plugins.CharGen.SetCurrentBrush)
            {
               _global.skse.plugins.CharGen.SetCurrentBrush(_loc3_.type);
            }
         }
         this._typeFilter.changeFilterData(this.categoryList.selectedEntry.flag,this.categoryList.selectedEntry.textFilter);
      }
   }
   function getBrushName(a_type)
   {
      switch(a_type)
      {
         case BrushWindow.BRUSH_TYPE_MASK_ADD:
            return "$Mask Add";
         case BrushWindow.BRUSH_TYPE_MASK_SUB:
            return "$Mask Subtract";
         case BrushWindow.BRUSH_TYPE_INFLATE:
            return "$Inflate";
         case BrushWindow.BRUSH_TYPE_DEFLATE:
            return "$Deflate";
         case BrushWindow.BRUSH_TYPE_MOVE:
            return "$Move";
         case BrushWindow.BRUSH_TYPE_SMOOTH:
            return "$Smooth";
         default:
            return "$Invalid";
      }
   }
   function getBrushIndex(a_brushId)
   {
      var _loc2_ = 0;
      while(_loc2_ < this.brushes.length)
      {
         if(this.brushes[_loc2_].type == a_brushId)
         {
            return _loc2_;
         }
         _loc2_ = _loc2_ + 1;
      }
      return -1;
   }
}
