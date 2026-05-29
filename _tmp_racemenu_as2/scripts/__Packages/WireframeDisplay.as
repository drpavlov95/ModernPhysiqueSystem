class WireframeDisplay extends gfx.core.UIComponent
{
   var _dragOffset;
   var _parent;
   var _x;
   var _xmouse;
   var _y;
   var _ymouse;
   var background;
   var dispatchEvent;
   var doHoverMesh;
   var doPaintMesh;
   var doPanMesh;
   var doRotateMesh;
   var editorData;
   var endPaintMesh;
   var endPanMesh;
   var endPanMeshAux;
   var endRotateMesh;
   var endRotateMeshAux;
   var fixedHeight;
   var fixedWidth;
   var foreground;
   var onMouseMove;
   var onMouseUp;
   var onRelease;
   var onReleaseAux;
   var onReleaseOutside;
   var onReleaseOutsideAux;
   var painting;
   var panning;
   var rotating;
   var wireframe;
   var buttonCount = 0;
   var paddingTop = 25;
   var paddingBottom = 25;
   var paddingLeft = 25;
   var paddingRight = 25;
   var _secondary = false;
   var bLoadedAssets = false;
   var disableInput = false;
   function WireframeDisplay()
   {
      super();
      _imageLoader = new MovieClipLoader();
      _imageLoader.addListener(this);
      Mouse.addListener(this);
      gfx.events.EventDispatcher.initialize(this);
   }
   function onLoad()
   {
      super.onLoad();
      gfx.events.EventDispatcher.initialize(this.background);
      this.background.addEventListener("press",this,"beginDrag");
      this.background.addEventListener("scroll",this,"onScrollWheel");
      this.background.onPress = function(controllerIdx, keyboardOrMouse, button)
      {
         if(this.disabled)
         {
            return undefined;
         }
         this.dispatchEvent({type:"press",controllerIdx:controllerIdx,button:button});
      };
   }
   function loadAssets()
   {
      if(this.bLoadedAssets)
      {
         return true;
      }
      if(_global.skse.plugins.CharGen.CreateMorphEditor)
      {
         this.editorData = _global.skse.plugins.CharGen.CreateMorphEditor();
         this.wireframe = this.foreground.createEmptyMovieClip("wireframe",this.foreground.getNextHighestDepth());
         _imageLoader.loadClip("img://headMesh",this.wireframe);
         this.bLoadedAssets = true;
         return true;
      }
      return false;
   }
   function unloadAssets()
   {
      if(!this.bLoadedAssets)
      {
         return false;
      }
      if(this.wireframe)
      {
         this.wireframe.removeMovieClip();
         this.wireframe = null;
      }
      if(_global.skse.plugins.CharGen.ReleaseMorphEditor)
      {
         _global.skse.plugins.CharGen.ReleaseMorphEditor();
         this.bLoadedAssets = false;
         return true;
      }
      return false;
   }
   function calculateBackground()
   {
      this.background._width = this.paddingLeft + this.foreground._width + this.paddingRight;
      this.background._height = this.paddingTop + this.foreground._height + this.paddingBottom;
      this.foreground._x = this.paddingLeft - this.background._width / 2;
      this.foreground._y = this.paddingTop - this.background._height / 2;
   }
   function onLoadInit(a_clip)
   {
      gfx.events.EventDispatcher.initialize(a_clip);
      this.foreground._width = 32;
      this.foreground._height = 32;
      a_clip._width = this.editorData.width;
      a_clip._height = this.editorData.height;
      this.foreground.fixedWidth = this.editorData.width;
      this.foreground.fixedHeight = this.editorData.height;
      this.foreground.doHoverMesh = function()
      {
         var _loc3_ = this.fixedWidth;
         var _loc4_ = this.fixedHeight;
         var _loc6_ = Math.max(0,Math.min(this._xmouse,_loc3_));
         var _loc5_ = Math.max(0,Math.min(this._ymouse,_loc4_));
         _global.skse.plugins.CharGen.DoHoverMesh(_loc6_,_loc5_);
      };
      this.foreground.doRotateMesh = function()
      {
         var _loc5_ = this.fixedWidth;
         var _loc6_ = this.fixedHeight;
         var _loc4_ = Math.max(0,Math.min(this._xmouse,_loc5_));
         var _loc3_ = Math.max(0,Math.min(this._ymouse,_loc6_));
         this.doHoverMesh(_loc4_,_loc3_);
         _global.skse.plugins.CharGen.DoRotateMesh(_loc4_,_loc3_);
      };
      this.foreground.endRotateMesh = function(mouseIdx, keyboardOrMouse, buttonIdx)
      {
         if(this.painting)
         {
            return undefined;
         }
         if(buttonIdx != 1)
         {
            return undefined;
         }
         this.rotating = false;
         this.onMouseMove = this.doHoverMesh;
         this.onReleaseAux = null;
         this.onReleaseOutsideAux = null;
         this._parent.dispatchEvent({type:"endRotating"});
         _global.skse.plugins.CharGen.EndRotateMesh();
      };
      this.foreground.endRotateMeshAux = function(keyboardOrMouse, buttonIdx)
      {
         return this.endRotateMesh(0,keyboardOrMouse,buttonIdx);
      };
      this.foreground.doPanMesh = function()
      {
         var _loc5_ = this.fixedWidth;
         var _loc6_ = this.fixedHeight;
         var _loc4_ = Math.max(0,Math.min(this._xmouse,_loc5_));
         var _loc3_ = Math.max(0,Math.min(this._ymouse,_loc6_));
         this.doHoverMesh(_loc4_,_loc3_);
         _global.skse.plugins.CharGen.DoPanMesh(_loc4_,_loc3_);
      };
      this.foreground.endPanMesh = function(mouseIdx, keyboardOrMouse, buttonIdx)
      {
         if(this.painting)
         {
            return undefined;
         }
         if(buttonIdx != 1)
         {
            return undefined;
         }
         this.panning = false;
         this.onMouseMove = this.doHoverMesh;
         this.onReleaseAux = null;
         this.onReleaseOutsideAux = null;
         this._parent.dispatchEvent({type:"endPanning"});
         _global.skse.plugins.CharGen.EndPanMesh();
      };
      this.foreground.endPanMeshAux = function(keyboardOrMouse, buttonIdx)
      {
         return this.endPanMesh(0,keyboardOrMouse,buttonIdx);
      };
      this.foreground.beginRotateMesh = function(mouseIdx, keyboardOrMouse, buttonIdx)
      {
         if(this.painting)
         {
            return undefined;
         }
         if(buttonIdx != 1)
         {
            return undefined;
         }
         var _loc3_;
         var _loc4_;
         var _loc6_;
         var _loc5_;
         if(!this._parent.secondary)
         {
            this.rotating = true;
            this.onMouseMove = this.doRotateMesh;
            this.onReleaseAux = this.endRotateMesh;
            this.onReleaseOutsideAux = this.endRotateMeshAux;
            this._parent.dispatchEvent({type:"beginRotating"});
            _loc3_ = this.fixedWidth;
            _loc4_ = this.fixedHeight;
            _loc6_ = Math.max(0,Math.min(this._xmouse,_loc3_));
            _loc5_ = Math.max(0,Math.min(this._ymouse,_loc4_));
            _global.skse.plugins.CharGen.BeginRotateMesh(_loc6_,_loc5_);
         }
         else
         {
            this.panning = true;
            this.onMouseMove = this.doPanMesh;
            this.onReleaseAux = this.endPanMesh;
            this.onReleaseOutsideAux = this.endPanMeshAux;
            this._parent.dispatchEvent({type:"beginPanning"});
            _loc3_ = this.fixedWidth;
            _loc4_ = this.fixedHeight;
            _loc6_ = Math.max(0,Math.min(this._xmouse,_loc3_));
            _loc5_ = Math.max(0,Math.min(this._ymouse,_loc4_));
            _global.skse.plugins.CharGen.BeginPanMesh(_loc6_,_loc5_);
         }
      };
      this.foreground.onPressAux = this.foreground.beginRotateMesh;
      this.foreground.doPaintMesh = function()
      {
         var _loc5_ = this.fixedWidth;
         var _loc6_ = this.fixedHeight;
         var _loc4_ = Math.max(0,Math.min(this._xmouse,_loc5_));
         var _loc3_ = Math.max(0,Math.min(this._ymouse,_loc6_));
         this.doHoverMesh(_loc4_,_loc3_);
         _global.skse.plugins.CharGen.DoPaintMesh(_loc4_,_loc3_);
      };
      this.foreground.endPaintMesh = function(mouseIdx, keyboardOrMouse, buttonIdx)
      {
         if(this.rotating || this.panning)
         {
            return undefined;
         }
         this.painting = false;
         this.onMouseMove = this.doHoverMesh;
         this.onRelease = null;
         this.onReleaseOutside = null;
         this._parent.dispatchEvent({type:"endPainting"});
         _global.skse.plugins.CharGen.EndPaintMesh();
      };
      this.foreground.beginPaintMesh = function(mouseIdx, keyboardOrMouse, buttonIdx)
      {
         if(this.rotating || this.panning)
         {
            return undefined;
         }
         if(this.onMouseMove != this.doHoverMesh)
         {
            return undefined;
         }
         var _loc3_ = this.fixedWidth;
         var _loc4_ = this.fixedHeight;
         var _loc6_ = Math.max(0,Math.min(this._xmouse,_loc3_));
         var _loc5_ = Math.max(0,Math.min(this._ymouse,_loc4_));
         if(_global.skse.plugins.CharGen.BeginPaintMesh(_loc6_,_loc5_))
         {
            this.painting = true;
            this.onMouseMove = this.doPaintMesh;
            this.onRelease = this.endPaintMesh;
            this.onReleaseOutside = this.onRelease;
            this._parent.dispatchEvent({type:"beginPainting"});
         }
         else
         {
            this._parent.background.onPress(mouseIdx,keyboardOrMouse,buttonIdx);
         }
      };
      this.foreground.onMouseMove = this.foreground.doHoverMesh;
      this.foreground.onPress = this.foreground.beginPaintMesh;
      this.calculateBackground();
   }
   function onMouseWheel(a_delta)
   {
      if(this.disableInput)
      {
         return undefined;
      }
      var _loc3_ = Mouse.getTopMostEntity();
      while(_loc3_ && _loc3_ != undefined)
      {
         if(_loc3_ == this)
         {
            if(a_delta < 0)
            {
               _global.skse.plugins.CharGen.SetMeshCameraRadius(_global.skse.plugins.CharGen.GetMeshCameraRadius() + 1);
            }
            else if(a_delta > 0)
            {
               _global.skse.plugins.CharGen.SetMeshCameraRadius(_global.skse.plugins.CharGen.GetMeshCameraRadius() - 1);
            }
            this.calculateBackground();
         }
         _loc3_ = _loc3_._parent;
      }
   }
   function set secondary(a_secondary)
   {
      this._secondary = a_secondary;
   }
   function get secondary()
   {
      return this._secondary;
   }
   function beginDrag(event)
   {
      this.onMouseMove = this.doDrag;
      this.onMouseUp = this.endDrag;
      this._dragOffset = {x:this._xmouse,y:this._ymouse};
      this.dispatchEvent({type:event.type,controllerIdx:event.controllerIdx,button:event.button});
   }
   function doDrag()
   {
      var _loc3_ = this._xmouse - this._dragOffset.x;
      var _loc2_ = this._ymouse - this._dragOffset.y;
      this._x += _loc3_;
      this._y += _loc2_;
   }
   function endDrag()
   {
      delete this.onMouseUp;
      delete this.onMouseMove;
   }
}
