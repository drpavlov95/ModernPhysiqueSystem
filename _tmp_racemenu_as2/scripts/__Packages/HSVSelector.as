class HSVSelector extends MovieClip
{
   var _alphaValue;
   var _currentColor;
   var _hsv;
   var aSlider;
   var dispatchEvent;
   var hSlider;
   var sSlider;
   var sliders;
   var vSlider;
   function HSVSelector()
   {
      super();
      this.hSlider = this.sliders.hSlider;
      this.sSlider = this.sliders.sSlider;
      this.vSlider = this.sliders.vSlider;
      this.aSlider = this.sliders.aSlider;
      this.setupGradients();
      gfx.events.EventDispatcher.initialize(this);
   }
   function onLoad()
   {
      this.hSlider.minimum = 0;
      this.hSlider.maximum = 360;
      this.hSlider.liveDragging = true;
      this.sSlider.minimum = 0;
      this.sSlider.maximum = 100;
      this.sSlider.liveDragging = true;
      this.vSlider.minimum = 0;
      this.vSlider.maximum = 100;
      this.vSlider.liveDragging = true;
      this.aSlider.minimum = 0;
      this.aSlider.maximum = 100;
      this.aSlider.liveDragging = true;
      this.hSlider.addEventListener("change",this,"onHSliderChange");
      this.sSlider.addEventListener("change",this,"onSSliderChange");
      this.vSlider.addEventListener("change",this,"onVSliderChange");
      this.aSlider.addEventListener("change",this,"onASliderChange");
      this.setColor(0,255);
   }
   function setColor(a_color, a_alpha)
   {
      var _loc3_ = a_alpha / 255 * 100;
      this._hsv = skyui.util.ColorFunctions.hexToHsv(a_color);
      var _loc4_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],100,this._hsv[2]]);
      var _loc5_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],this._hsv[1],100]);
      this.hSlider.track.trackWhite._alpha = 100 - this._hsv[1];
      this.hSlider.track.trackBlack._alpha = 100 - this._hsv[2];
      this.sSlider.track.trackBlack._alpha = 100 - this._hsv[2];
      this.vSlider.track.trackWhite._alpha = 100 - this._hsv[1];
      this.aSlider.track.trackColor._alpha = _loc3_;
      var _loc2_ = new Color(this.sSlider.track.trackColor);
      _loc2_.setRGB(_loc4_);
      _loc2_ = new Color(this.vSlider.track.trackColor);
      _loc2_.setRGB(_loc5_);
      _loc2_ = new Color(this.aSlider.track.trackColor);
      _loc2_.setRGB(a_color);
      this.hSlider.value = this._hsv[0];
      this.sSlider.value = this._hsv[1];
      this.vSlider.value = this._hsv[2];
      this.aSlider.value = _loc3_;
      this._currentColor = a_color;
      this._alphaValue = a_alpha;
   }
   function onHSliderChange(a_event)
   {
      var _loc4_ = a_event.target.value;
      this._hsv[0] = _loc4_;
      var _loc3_ = skyui.util.ColorFunctions.hsvToHex(this._hsv);
      var _loc5_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],100,this._hsv[2]]);
      var _loc6_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],this._hsv[1],100]);
      var _loc2_ = new Color(this.sSlider.track.trackColor);
      _loc2_.setRGB(_loc5_);
      _loc2_ = new Color(this.vSlider.track.trackColor);
      _loc2_.setRGB(_loc6_);
      _loc2_ = new Color(this.aSlider.track.trackColor);
      _loc2_.setRGB(_loc3_);
      this._currentColor = _loc3_;
      this.dispatchEvent({type:"changeColor",color:this._currentColor,alpha:this._alphaValue});
   }
   function onSSliderChange(a_event)
   {
      var _loc5_ = a_event.target.value;
      this._hsv[1] = _loc5_;
      var _loc3_ = skyui.util.ColorFunctions.hsvToHex(this._hsv);
      var _loc4_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],this._hsv[1],100]);
      this.hSlider.track.trackWhite._alpha = 100 - this._hsv[1];
      this.vSlider.track.trackWhite._alpha = 100 - this._hsv[1];
      var _loc2_ = new Color(this.vSlider.track.trackColor);
      _loc2_.setRGB(_loc4_);
      _loc2_ = new Color(this.aSlider.track.trackColor);
      _loc2_.setRGB(_loc3_);
      this._currentColor = _loc3_;
      this.dispatchEvent({type:"changeColor",color:this._currentColor,alpha:this._alphaValue});
   }
   function onVSliderChange(a_event)
   {
      var _loc5_ = a_event.target.value;
      this._hsv[2] = _loc5_;
      var _loc3_ = skyui.util.ColorFunctions.hsvToHex(this._hsv);
      var _loc4_ = skyui.util.ColorFunctions.hsvToHex([this._hsv[0],100,this._hsv[2]]);
      this.hSlider.track.trackBlack._alpha = 100 - this._hsv[2];
      this.sSlider.track.trackBlack._alpha = 100 - this._hsv[2];
      var _loc2_ = new Color(this.sSlider.track.trackColor);
      _loc2_.setRGB(_loc4_);
      _loc2_ = new Color(this.aSlider.track.trackColor);
      _loc2_.setRGB(_loc3_);
      this._currentColor = _loc3_;
      this.dispatchEvent({type:"changeColor",color:this._currentColor,alpha:this._alphaValue});
   }
   function onASliderChange(a_event)
   {
      var _loc2_ = a_event.target.value;
      var _loc4_ = skyui.util.ColorFunctions.hsvToHex(this._hsv);
      var _loc3_ = _loc2_ / 100 * 255;
      this.aSlider.track.trackColor._alpha = _loc2_;
      this._currentColor = _loc4_;
      this._alphaValue = _loc3_;
      this.dispatchEvent({type:"changeColor",color:this._currentColor,alpha:this._alphaValue});
   }
   function getColor()
   {
      return this._currentColor;
   }
   function getAlpha()
   {
      return this._alphaValue;
   }
   function setupGradients()
   {
      var _loc10_ = this.hSlider.track;
      var _loc9_ = this.sSlider.track;
      var _loc8_ = this.vSlider.track;
      var _loc12_ = this.aSlider.track;
      var _loc3_;
      var _loc4_;
      var _loc14_;
      var _loc13_;
      var _loc15_;
      var _loc7_;
      var _loc2_;
      var _loc5_;
      var _loc6_;
      var _loc11_;
      _loc3_ = _loc10_._width;
      _loc4_ = _loc10_._height;
      _loc14_ = [16711680,16776960,65280,65535,255,16711935,16711680];
      _loc13_ = [100,100,100,100,100,100,100];
      _loc15_ = [0,42.5,85,127.5,170,212.5,255];
      _loc7_ = new flash.geom.Matrix();
      _loc7_.createGradientBox(_loc3_,_loc4_);
      _loc2_ = _loc10_.createEmptyMovieClip("trackColor",_loc10_.getNextHighestDepth());
      _loc2_.beginGradientFill("linear",_loc14_,_loc13_,_loc15_,_loc7_);
      _loc2_.moveTo(0,0);
      _loc2_.lineTo(0,_loc4_);
      _loc2_.lineTo(_loc3_,_loc4_);
      _loc2_.lineTo(_loc3_,0);
      _loc2_.lineTo(0,0);
      _loc2_.endFill();
      _loc5_ = _loc10_.createEmptyMovieClip("trackWhite",_loc10_.getNextHighestDepth());
      _loc5_.beginFill(16777215);
      _loc5_.moveTo(0,0);
      _loc5_.lineTo(0,_loc4_);
      _loc5_.lineTo(_loc3_,_loc4_);
      _loc5_.lineTo(_loc3_,0);
      _loc5_.lineTo(0,0);
      _loc5_.endFill();
      _loc6_ = _loc10_.createEmptyMovieClip("trackBlack",_loc10_.getNextHighestDepth());
      _loc6_.beginFill(0);
      _loc6_.moveTo(0,0);
      _loc6_.lineTo(0,_loc4_);
      _loc6_.lineTo(_loc3_,_loc4_);
      _loc6_.lineTo(_loc3_,0);
      _loc6_.lineTo(0,0);
      _loc6_.endFill();
      _loc3_ = _loc9_._width;
      _loc4_ = _loc9_._height;
      _loc2_ = _loc9_.createEmptyMovieClip("trackColor",_loc9_.getNextHighestDepth());
      _loc2_.beginFill(0);
      _loc2_.moveTo(0,0);
      _loc2_.lineTo(_loc3_,0);
      _loc2_.lineTo(_loc3_,_loc4_);
      _loc2_.lineTo(0,_loc4_);
      _loc2_.lineTo(0,0);
      _loc2_.endFill();
      _loc14_ = [16777215,0];
      _loc13_ = [100,0];
      _loc15_ = [0,255];
      _loc7_ = new flash.geom.Matrix();
      _loc7_.createGradientBox(_loc3_,_loc4_);
      _loc5_ = _loc9_.createEmptyMovieClip("trackWhite",_loc9_.getNextHighestDepth());
      _loc5_.beginGradientFill("linear",_loc14_,_loc13_,_loc15_,_loc7_);
      _loc5_.moveTo(0,0);
      _loc5_.lineTo(_loc3_,0);
      _loc5_.lineTo(_loc3_,_loc4_);
      _loc5_.lineTo(0,_loc4_);
      _loc5_.lineTo(0,0);
      _loc5_.endFill();
      _loc6_ = _loc9_.createEmptyMovieClip("trackBlack",_loc9_.getNextHighestDepth());
      _loc6_.beginFill(0);
      _loc6_.moveTo(0,0);
      _loc6_.lineTo(_loc3_,0);
      _loc6_.lineTo(_loc3_,_loc4_);
      _loc6_.lineTo(0,_loc4_);
      _loc6_.lineTo(0,0);
      _loc6_.endFill();
      _loc3_ = _loc8_._width;
      _loc4_ = _loc8_._height;
      _loc2_ = _loc8_.createEmptyMovieClip("trackColor",_loc8_.getNextHighestDepth());
      _loc2_.beginFill(0);
      _loc2_.moveTo(0,0);
      _loc2_.lineTo(_loc3_,0);
      _loc2_.lineTo(_loc3_,_loc4_);
      _loc2_.lineTo(0,_loc4_);
      _loc2_.lineTo(0,0);
      _loc2_.endFill();
      _loc5_ = _loc8_.createEmptyMovieClip("trackWhite",_loc8_.getNextHighestDepth());
      _loc5_.beginFill(16777215);
      _loc5_.moveTo(0,0);
      _loc5_.lineTo(_loc3_,0);
      _loc5_.lineTo(_loc3_,_loc4_);
      _loc5_.lineTo(0,_loc4_);
      _loc5_.lineTo(0,0);
      _loc5_.endFill();
      _loc14_ = [0,0];
      _loc13_ = [100,0];
      _loc15_ = [0,255];
      _loc7_ = new flash.geom.Matrix();
      _loc7_.createGradientBox(_loc3_,_loc4_);
      _loc6_ = _loc8_.createEmptyMovieClip("trackBlack",_loc8_.getNextHighestDepth());
      _loc6_.beginGradientFill("linear",_loc14_,_loc13_,_loc15_,_loc7_);
      _loc6_.moveTo(0,0);
      _loc6_.lineTo(_loc3_,0);
      _loc6_.lineTo(_loc3_,_loc4_);
      _loc6_.lineTo(0,_loc4_);
      _loc6_.lineTo(0,0);
      _loc6_.endFill();
      _loc3_ = _loc12_._width;
      _loc4_ = _loc12_._height;
      var _loc16_ = true;
      _loc7_ = new flash.geom.Matrix();
      var _loc17_ = flash.display.BitmapData.loadBitmap("AlphaBackground");
      _loc11_ = _loc12_.createEmptyMovieClip("trackAlpha",_loc12_.getNextHighestDepth());
      _loc11_.beginBitmapFill(_loc17_,_loc7_,_loc16_);
      _loc11_.moveTo(0,0);
      _loc11_.lineTo(_loc3_,0);
      _loc11_.lineTo(_loc3_,_loc4_);
      _loc11_.lineTo(0,_loc4_);
      _loc11_.lineTo(0,0);
      _loc11_.endFill();
      _loc2_ = _loc12_.createEmptyMovieClip("trackColor",_loc12_.getNextHighestDepth());
      _loc2_.beginFill(0);
      _loc2_.moveTo(0,0);
      _loc2_.lineTo(_loc3_,0);
      _loc2_.lineTo(_loc3_,_loc4_);
      _loc2_.lineTo(0,_loc4_);
      _loc2_.lineTo(0,0);
      _loc2_.endFill();
   }
}
