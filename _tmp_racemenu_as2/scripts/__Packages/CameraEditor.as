class CameraEditor extends MovieClip
{
   var _acceptControl;
   var _activateControl;
   var _cancelControl;
   var _downControl;
   var _initCameraPos;
   var _initPlayerPos;
   var _leftControl;
   var _lrControl;
   var _modifierControl;
   var _moveVector;
   var _platform;
   var _rateControl;
   var _rightControl;
   var _udControl;
   var _udlrControl;
   var _upControl;
   var _zoomVector;
   var bottomBar;
   var navPanel;
   var BOTTOMBAR_SHOWN_Y = 645;
   var BOTTOMBAR_HIDDEN_Y = 745;
   var _startRotation = 0;
   var _currentRotation = 0;
   var _secondary = false;
   var movementRate = 5;
   function CameraEditor()
   {
      super();
      Mouse.addListener(this);
      gfx.events.EventDispatcher.initialize(this);
      this.navPanel = this.bottomBar.buttonPanel;
      this.bottomBar._y = this.BOTTOMBAR_HIDDEN_Y;
   }
   function onLoad()
   {
      super.onLoad();
      this.bottomBar.hidePlayerInfo();
   }
   function InitExtensions()
   {
      var _loc3_ = _global.skse.plugins.CharGen.GetPlayerRotation();
      this._startRotation = Math.acos(_loc3_[0]);
      this._currentRotation = 0;
      this.bottomBar.positionBackground();
   }
   function setPlatform(a_platform, a_bPS3Switch)
   {
      this._platform = a_platform;
      this.bottomBar.setPlatform(a_platform,a_bPS3Switch);
      this._activateControl = skyui.defines.Input.Accept;
      if(this._platform == 0)
      {
         this._cancelControl = {name:"Tween Menu",context:skyui.defines.Input.CONTEXT_GAMEPLAY};
      }
      else
      {
         this._cancelControl = skyui.defines.Input.Cancel;
      }
      this._acceptControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Ready Weapon",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      this._upControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Up",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._downControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Down",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._leftControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Left",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._rightControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Right",skyui.defines.Input.CONTEXT_MENUMODE,a_platform != 0)};
      this._udlrControl = [this._upControl,this._downControl,this._leftControl,this._rightControl];
      this._lrControl = [this._leftControl,this._rightControl];
      this._udControl = [this._upControl,this._downControl];
      this._rateControl = [this._activateControl,this._cancelControl];
      this._modifierControl = {keyCode:skyui.util.GlobalFunctions.getMappedKey("Sprint",skyui.defines.Input.CONTEXT_GAMEPLAY,a_platform != 0)};
      var _loc4_ = Stage.visibleRect.x + Stage.safeRect.x;
      var _loc3_ = Stage.visibleRect.x + Stage.visibleRect.width - Stage.safeRect.x;
      this.bottomBar.positionElements(_loc4_,_loc3_);
      this.updateBottomBar();
   }
   function updateBottomBar()
   {
      this.navPanel.clearButtons();
      this.navPanel.addButton({text:"$Done",controls:this._acceptControl}).addEventListener("click",this._parent,"onDoneClicked");
      if(!this._secondary)
      {
         this.navPanel.addButton({text:"$Secondary",controls:this._modifierControl});
      }
      else
      {
         this.navPanel.addButton({text:"$Primary",controls:this._modifierControl});
      }
      if(!this._secondary)
      {
         this.navPanel.addButton({text:"$Move Camera",controls:this._udlrControl});
      }
      if(this._secondary)
      {
         this.navPanel.addButton({text:"$Zoom",controls:this._udControl});
         this.navPanel.addButton({text:"$Rotate",controls:this._lrControl});
      }
      this.navPanel.addButton({text:skyui.util.Translator.translateNested("$Rate ({" + this.movementRate + "})"),controls:this._rateControl});
      this.navPanel.updateButtons(true);
   }
   function ShowAll(bShowAll)
   {
      this.ShowBottomBar(bShowAll);
      if(bShowAll)
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:100,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this,0.5,{autoAlpha:0,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      this.enabled = bShowAll;
   }
   function ShowBottomBar(bShowBottomBar)
   {
      if(bShowBottomBar)
      {
         com.greensock.TweenLite.to(this.bottomBar,0.5,{autoAlpha:100,_y:this.BOTTOMBAR_SHOWN_Y,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
      else
      {
         com.greensock.TweenLite.to(this.bottomBar,0.5,{autoAlpha:0,_y:this.BOTTOMBAR_HIDDEN_Y,overwrite:com.greensock.OverwriteManager.NONE,easing:com.greensock.easing.Linear.easeNone});
      }
   }
   function IsBoundKeyPressed(details, boundKey, platform)
   {
      return details.control && details.control == boundKey.name || details.skseKeycode && boundKey.name && boundKey.context && details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey(boundKey.name,Number(boundKey.context),platform != 0) || details.skseKeycode && details.skseKeycode == boundKey.keyCode;
   }
   function handleInput(details, pathToFocus)
   {
      if(details.skseKeycode == this._modifierControl.keyCode)
      {
         this._secondary = details.value == "keyDown" || details.value == "keyHold";
         this.updateBottomBar(true);
         return true;
      }
      if(!this._initCameraPos)
      {
         this._initCameraPos = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
      }
      var _loc8_;
      var _loc6_;
      var _loc10_;
      var _loc7_;
      var _loc9_;
      if(!this._initPlayerPos)
      {
         this._initPlayerPos = _global.skse.plugins.CharGen.GetPlayerPosition();
         _loc8_ = this._initCameraPos.x - this._initPlayerPos.x;
         _loc6_ = this._initCameraPos.y - this._initPlayerPos.y;
         _loc10_ = Math.sqrt(Math.pow(_loc8_,2) + Math.pow(_loc6_,2));
         _loc7_ = _loc8_ / _loc10_;
         _loc9_ = _loc6_ / _loc10_;
         this._zoomVector = {x:_loc7_,y:_loc9_};
         this._moveVector = {x:_loc9_,y:- _loc7_};
      }
      var _loc4_ = details.value == "keyDown" || details.value == "keyHold";
      if((details.navEquivalent == gfx.ui.NavigationCode.ENTER || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Activate",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0)) && _loc4_)
      {
         this.movementRate += 0.5;
         this.updateBottomBar(true);
         return true;
      }
      if((details.navEquivalent == gfx.ui.NavigationCode.TAB || details.skseKeycode == skyui.util.GlobalFunctions.getMappedKey("Cancel",skyui.defines.Input.CONTEXT_GAMEPLAY,this._platform != 0)) && _loc4_)
      {
         this.movementRate -= 0.5;
         if(this.movementRate < 0.5)
         {
            this.movementRate = 0.5;
         }
         this.updateBottomBar(true);
         return true;
      }
      var _loc5_;
      if(details.navEquivalent === gfx.ui.NavigationCode.RIGHT)
      {
         if(_loc4_)
         {
            if(!this._secondary)
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.x -= this._moveVector.x * this.movementRate;
                  _loc5_.y -= this._moveVector.y * this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
            else
            {
               this.currentRotation += this.movementRate;
            }
         }
         return true;
      }
      if(details.navEquivalent === gfx.ui.NavigationCode.LEFT)
      {
         if(_loc4_)
         {
            if(!this._secondary)
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.x += this._moveVector.x * this.movementRate;
                  _loc5_.y += this._moveVector.y * this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
            else
            {
               this.currentRotation -= this.movementRate;
            }
         }
         return true;
      }
      if(details.navEquivalent === gfx.ui.NavigationCode.UP)
      {
         if(_loc4_)
         {
            if(!this._secondary)
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.z += this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
            else
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.x -= this._zoomVector.x * this.movementRate;
                  _loc5_.y -= this._zoomVector.y * this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
         }
         return true;
      }
      if(details.navEquivalent === gfx.ui.NavigationCode.DOWN)
      {
         if(_loc4_)
         {
            if(!this._secondary)
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.z -= this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
            else
            {
               _loc5_ = _global.skse.plugins.CharGen.GetRaceSexCameraPos();
               if(_loc5_)
               {
                  _loc5_.x += this._zoomVector.x * this.movementRate;
                  _loc5_.y += this._zoomVector.y * this.movementRate;
                  _global.skse.plugins.CharGen.SetRaceSexCameraPos(_loc5_);
               }
            }
         }
         return true;
      }
      return false;
   }
   function get currentRotation()
   {
      return this._currentRotation * 180 / 3.141592653589793;
   }
   function set currentRotation(a_number)
   {
      this._currentRotation = a_number * 3.141592653589793 / 180;
      var _loc3_;
      if(_global.skse.plugins.CharGen.SetPlayerRotation)
      {
         _loc3_ = [Math.cos(this._currentRotation + this._startRotation),- Math.sin(this._currentRotation + this._startRotation),0,Math.sin(this._currentRotation + this._startRotation),Math.cos(this._currentRotation + this._startRotation),0,0,0,1];
         _global.skse.plugins.CharGen.SetPlayerRotation(_loc3_);
      }
   }
}
