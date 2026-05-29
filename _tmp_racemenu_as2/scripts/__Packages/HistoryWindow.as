class HistoryWindow extends MovableWindow
{
   var _parent;
   var historyList;
   var lastEntry;
   var onInvalidate;
   var bLoadedAssets = false;
   static var UNDO_TYPE_NONE = 0;
   static var UNDO_TYPE_STROKE = 1;
   static var UNDO_TYPE_RESETMASK = 2;
   static var UNDO_TYPE_RESETSCULPT = 3;
   static var UNDO_TYPE_IMPORTGEOMETRY = 4;
   static var STROKE_TYPE_NONE = 0;
   static var STROKE_TYPE_MASK_ADD = 1;
   static var STROKE_TYPE_MASK_SUB = 2;
   static var STROKE_TYPE_INFLATE = 3;
   static var STROKE_TYPE_DEFLATE = 4;
   static var STROKE_TYPE_MOVE = 5;
   static var STROKE_TYPE_SMOOTH = 6;
   function HistoryWindow()
   {
      super();
      this.historyList.disableSelection = this.historyList.disableInput = true;
   }
   function onLoad()
   {
      super.onLoad();
      this.historyList.listEnumeration = new skyui.components.list.BasicEnumeration(this.historyList.entryList);
      this.historyList.historyIndex = -1;
      this.historyList.addEventListener("itemPress",this,"onItemPress");
      this.historyList.addEventListener("selectionChange",this,"onSelectionChange");
      this.historyList.requestInvalidate();
   }
   function InitExtensions()
   {
      gfx.io.GameDelegate.addCallBack("AddAction",this,"onAddAction");
   }
   function onSelectionChange(event)
   {
      var _loc2_ = this.historyList.entryList[event.index];
      this.historyList.listState.selectedEntry = _loc2_;
      this.historyList.requestUpdate();
      this._parent._parent.setStatusText(_loc2_.part);
   }
   function onItemPress(event)
   {
      var _loc4_;
      var _loc3_;
      if(event.index >= 0)
      {
         _loc4_ = this.historyList.historyIndex;
         _loc3_ = _global.skse.plugins.CharGen.GoToAction(event.index);
         if(_loc4_ == _loc3_ && _loc3_ == 0)
         {
            _loc3_ = _global.skse.plugins.CharGen.GoToAction(-1);
         }
         this.historyList.historyIndex = _loc3_;
         this.historyList.requestUpdate();
      }
   }
   function onAddAction(action)
   {
      if(this.historyList.historyIndex < this.historyList.entryList.length - 1)
      {
         this.historyList.entryList.splice(this.historyList.historyIndex + 1,this.historyList.entryList.length - this.historyList.historyIndex);
      }
      if(this.historyList.entryList.length == _global.skse.plugins.CharGen.GetActionLimit())
      {
         this.historyList.entryList.splice(0,1);
      }
      action.text = this.createActionName(action);
      this.historyList.entryList.push(action);
      this.historyList.historyIndex = this.historyList.entryList.length - 1;
      this.historyList.requestInvalidate();
      this.historyList.onInvalidate = function()
      {
         this.lastEntry();
         delete this.onInvalidate;
      };
   }
   function createActionName(action)
   {
      var _loc1_ = "";
      var _loc2_ = "";
      if(action.vertices > 0)
      {
         _loc2_ = " (" + action.vertices + ")";
      }
      if(action.mirror == true)
      {
         _loc2_ += " (M)";
      }
      switch(action.type)
      {
         case HistoryWindow.UNDO_TYPE_STROKE:
            switch(action.stroke)
            {
               case HistoryWindow.STROKE_TYPE_MASK_ADD:
                  _loc1_ = "$Mask Add";
                  break;
               case HistoryWindow.STROKE_TYPE_MASK_SUB:
                  _loc1_ = "$Mask Subtract";
                  break;
               case HistoryWindow.STROKE_TYPE_INFLATE:
                  _loc1_ = "$Inflate";
                  break;
               case HistoryWindow.STROKE_TYPE_DEFLATE:
                  _loc1_ = "$Deflate";
                  break;
               case HistoryWindow.STROKE_TYPE_MOVE:
                  _loc1_ = "$Move";
                  break;
               case HistoryWindow.STROKE_TYPE_SMOOTH:
                  _loc1_ = "$Smooth";
                  break;
               default:
                  _loc1_ = "Unknown Stroke";
            }
            break;
         case HistoryWindow.UNDO_TYPE_RESETMASK:
            _loc1_ = "$Clear Mask";
            break;
         case HistoryWindow.UNDO_TYPE_RESETSCULPT:
            _loc1_ = "$Clear Sculpt";
            break;
         case HistoryWindow.UNDO_TYPE_IMPORTGEOMETRY:
            _loc1_ = "$Import Geometry";
            break;
         default:
            _loc1_ = "Unknown Action";
      }
      return skyui.util.Translator.translateNested(_loc1_) + _loc2_;
   }
   function loadAssets()
   {
      if(this.bLoadedAssets)
      {
         return true;
      }
      this.historyList.disableSelection = this.historyList.disableInput = false;
      this.historyList.entryList = null;
      this.historyList.listEnumeration = new skyui.components.list.BasicEnumeration(this.historyList.entryList);
      this.historyList.requestInvalidate();
      this.bLoadedAssets = true;
      return true;
   }
   function unloadAssets()
   {
      if(!this.bLoadedAssets)
      {
         return false;
      }
      this.historyList.disableSelection = this.historyList.disableInput = true;
      this.historyList.entryList.splice(0,this.historyList.entryList.length);
      this.historyList.requestInvalidate();
      this.bLoadedAssets = false;
   }
}
