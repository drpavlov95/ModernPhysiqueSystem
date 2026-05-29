class com.greensock.plugins.TweenPlugin
{
   var _changeFactor;
   var _tweens;
   var overwriteProps;
   var propName;
   var round;
   static var VERSION = 1.4;
   static var API = 1;
   var priority = 0;
   function TweenPlugin()
   {
      this._tweens = [];
      this._changeFactor = 0;
   }
   function onInitTween(target, value, tween)
   {
      this.addTween(target,this.propName,target[this.propName],value,this.propName);
      return true;
   }
   function addTween(object, propName, start, end, overwriteProp)
   {
      var _loc3_;
      if(end != undefined)
      {
         _loc3_ = typeof end != "number" ? Number(end) : Number(end) - start;
         if(_loc3_ != 0)
         {
            this._tweens[this._tweens.length] = new com.greensock.core.PropTween(object,propName,start,_loc3_,overwriteProp || propName);
         }
      }
   }
   function updateTweens(changeFactor)
   {
      var _loc3_ = this._tweens.length;
      var _loc2_;
      var _loc4_;
      if(this.round)
      {
         while((_loc3_ = _loc3_ - 1) > -1)
         {
            _loc2_ = this._tweens[_loc3_];
            _loc4_ = _loc2_.start + _loc2_.change * changeFactor;
            if(_loc4_ > 0)
            {
               _loc2_.target[_loc2_.property] = _loc4_ + 0.5 >> 0;
            }
            else
            {
               _loc2_.target[_loc2_.property] = _loc4_ - 0.5 >> 0;
            }
         }
      }
      else
      {
         while((_loc3_ = _loc3_ - 1) > -1)
         {
            _loc2_ = this._tweens[_loc3_];
            _loc2_.target[_loc2_.property] = _loc2_.start + _loc2_.change * changeFactor;
         }
      }
   }
   function get changeFactor()
   {
      return this._changeFactor;
   }
   function set changeFactor(n)
   {
      this.updateTweens(n);
      this._changeFactor = n;
   }
   function killProps(lookup)
   {
      var _loc2_ = this.overwriteProps.length;
      while((_loc2_ = _loc2_ - 1) > -1)
      {
         if(lookup[this.overwriteProps[_loc2_]])
         {
            this.overwriteProps.splice(_loc2_,1);
         }
      }
      _loc2_ = this._tweens.length;
      while((_loc2_ = _loc2_ - 1) > -1)
      {
         if(lookup[this._tweens[_loc2_].name])
         {
            this._tweens.splice(_loc2_,1);
         }
      }
   }
   static function onTweenEvent(type, tween)
   {
      var _loc1_ = tween.cachedPT1;
      var _loc5_;
      var _loc3_;
      var _loc2_;
      var _loc0_;
      if(type == "onInitAllProps")
      {
         _loc3_ = [];
         _loc2_ = 0;
         while(_loc1_)
         {
            _loc3_[_loc2_++] = _loc1_;
            _loc1_ = _loc1_.nextNode;
         }
         _loc3_.sortOn("priority",Array.NUMERIC | Array.DESCENDING);
         while((_loc2_ = _loc2_ - 1) > -1)
         {
            _loc3_[_loc2_].nextNode = _loc3_[_loc2_ + 1];
            _loc3_[_loc2_].prevNode = _loc3_[_loc2_ - 1];
         }
         _loc1_ = tween.cachedPT1 = _loc3_[0];
      }
      while(_loc1_)
      {
         if(_loc1_.isPlugin && _loc1_.target[type])
         {
            if(_loc1_.target.activeDisable)
            {
               _loc5_ = true;
            }
            _loc1_.target[type]();
         }
         _loc1_ = _loc1_.nextNode;
      }
      return _loc5_;
   }
   static function activate(plugins)
   {
      com.greensock.TweenLite.onPluginEvent = com.greensock.plugins.TweenPlugin.onTweenEvent;
      var _loc1_ = plugins.length;
      var _loc3_;
      while(_loc1_--)
      {
         if(plugins[_loc1_].API == 1)
         {
            _loc3_ = new plugins[_loc1_]();
            com.greensock.TweenLite.plugins[_loc3_.propName] = plugins[_loc1_];
         }
      }
      return true;
   }
}
