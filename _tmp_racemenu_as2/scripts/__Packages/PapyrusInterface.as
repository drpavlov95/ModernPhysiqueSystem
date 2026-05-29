class PapyrusInterface
{
   var AddMakeup;
   var SetMakeup;
   var SetSliderColors;
   var UpdateTintCount;
   var _raceList;
   var categoryList;
   var customSliders;
   var itemList;
   var loadingIcon;
   var makeupList;
   static var _instance;
   function PapyrusInterface()
   {
   }
   static function initialize(parent)
   {
      if(PapyrusInterface._instance == undefined)
      {
         PapyrusInterface._instance = new PapyrusInterface();
      }
      parent.RSM_AddSliders = PapyrusInterface._instance.RSM_AddSliders;
      parent.RSM_SetSliderParameters = PapyrusInterface._instance.RSM_SetSliderParameters;
      parent.RSM_SetSliderParametersEx = PapyrusInterface._instance.RSM_SetSliderParametersEx;
      parent.RSM_AddWarpaints = PapyrusInterface._instance.RSM_AddWarpaints;
      parent.RSM_AddBodyPaints = PapyrusInterface._instance.RSM_AddBodyPaints;
      parent.RSM_AddHandPaints = PapyrusInterface._instance.RSM_AddHandPaints;
      parent.RSM_AddFeetPaints = PapyrusInterface._instance.RSM_AddFeetPaints;
      parent.RSM_AddFacePaints = PapyrusInterface._instance.RSM_AddFacePaints;
      parent.RSM_AddTints = PapyrusInterface._instance.RSM_AddTints;
      parent.RSM_AddBodyTints = PapyrusInterface._instance.RSM_AddBodyTints;
      parent.RSM_AddHandTints = PapyrusInterface._instance.RSM_AddHandTints;
      parent.RSM_AddFeetTints = PapyrusInterface._instance.RSM_AddFeetTints;
      parent.RSM_AddFaceTints = PapyrusInterface._instance.RSM_AddFaceTints;
      parent.RSM_ExtendRace = PapyrusInterface._instance.RSM_ExtendRace;
      parent.RSM_SaveClipboard = PapyrusInterface._instance.RSM_SaveClipboard;
      parent.RSM_LoadClipboard = PapyrusInterface._instance.RSM_LoadClipboard;
      parent.RSM_ToggleLoader = PapyrusInterface._instance.RSM_ToggleLoader;
      parent.RSM_SetPresetSlot = PapyrusInterface._instance.RSM_SetPresetSlot;
      parent.RSM_AddCategories = PapyrusInterface._instance.RSM_AddCategories;
   }
   function RSM_AddSliders()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc7_;
      var _loc10_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "")
         {
            _loc6_ = this.customSliders.length + RaceMenuDefines.CUSTOM_SLIDER_OFFSET;
            if(_loc3_.length >= 8)
            {
               _loc7_ = _loc3_[7].split("|");
            }
            if(_loc3_.length >= 9)
            {
               _loc10_ = Number(_loc3_[8]);
            }
            _loc5_ = {type:RaceMenuDefines.ENTRY_TYPE_SLIDER,text:_loc3_[0],filterFlag:Number(_loc3_[1]),textFilters:_loc7_,callbackName:_loc3_[2],sliderMin:Number(_loc3_[3]),sliderMax:Number(_loc3_[4]),sliderID:_loc6_,position:Number(_loc3_[6]),interval:Number(_loc3_[5]),priority:_loc10_,enabled:true};
            this.customSliders.push(_loc5_);
            this.itemList.entryList.push(_loc5_);
            this.itemList.requestInvalidate();
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddCategories()
   {
      var _loc6_ = 0;
      var _loc4_;
      var _loc7_;
      var _loc5_;
      var _loc3_;
      while(_loc6_ < arguments.length)
      {
         _loc4_ = arguments[_loc6_].split(";;");
         if(_loc4_[0] != "")
         {
            _loc7_ = {type:RaceMenuDefines.ENTRY_TYPE_CAT,bDontHide:false,filterFlag:1,text:_loc4_[1],flag:0,textFilter:_loc4_[0],priority:Number(_loc4_[2]),enabled:true};
            _loc5_ = false;
            _loc3_ = 0;
            while(_loc3_ < this.categoryList.entryList.length)
            {
               if(this.categoryList.entryList[_loc3_].textFilter == _loc4_[0])
               {
                  _loc5_ = true;
                  break;
               }
               _loc3_ = _loc3_ + 1;
            }
            if(!_loc5_)
            {
               this.categoryList.entryList.push(_loc7_);
               this.categoryList.requestInvalidate();
            }
         }
         _loc6_ = _loc6_ + 1;
      }
   }
   function RSM_SetSliderParameters()
   {
      var _loc3_ = 0;
      while(_loc3_ < this.customSliders.length)
      {
         if(this.customSliders[_loc3_].callbackName.toLowerCase() == arguments[0].toLowerCase())
         {
            this.customSliders[_loc3_].sliderMin = Number(arguments[1]);
            this.customSliders[_loc3_].sliderMax = Number(arguments[2]);
            this.customSliders[_loc3_].interval = Number(arguments[3]);
            this.customSliders[_loc3_].position = Number(arguments[4]);
            this.itemList.requestUpdate();
            break;
         }
         _loc3_ = _loc3_ + 1;
      }
   }
   function RSM_SetSliderParametersEx()
   {
      var _loc6_ = arguments[0].split(";;");
      var _loc7_ = arguments[1].split(";;");
      var _loc11_ = arguments[2].split(";;");
      var _loc9_ = arguments[3].split(";;");
      var _loc10_ = arguments[4].split(";;");
      var _loc8_ = arguments[5].split(";;");
      var _loc5_ = 0;
      var _loc3_;
      var _loc4_;
      while(_loc5_ < this.customSliders.length)
      {
         _loc3_ = 0;
         while(_loc3_ < _loc6_.length)
         {
            if(this.customSliders[_loc5_].callbackName.toLowerCase() == _loc6_[_loc3_].toLowerCase())
            {
               _loc4_ = Number(_loc8_[_loc3_]) >>> 0;
               if((_loc4_ & 2) == 0)
               {
                  this.customSliders[_loc5_].sliderMin = Number(_loc7_[_loc3_]);
               }
               if((_loc4_ & 4) == 0)
               {
                  this.customSliders[_loc5_].sliderMax = Number(_loc11_[_loc3_]);
               }
               if((_loc4_ & 8) == 0)
               {
                  this.customSliders[_loc5_].interval = Number(_loc9_[_loc3_]);
               }
               if((_loc4_ & 0x10) == 0)
               {
                  this.customSliders[_loc5_].position = Number(_loc10_[_loc3_]);
               }
               if((_loc4_ & 1) == 0)
               {
                  this.itemList.requestUpdate();
               }
               break;
            }
            _loc3_ = _loc3_ + 1;
         }
         _loc5_ = _loc5_ + 1;
      }
   }
   function RSM_AddWarpaints()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "" && _loc3_[1] != "")
         {
            _loc6_ = _loc3_[0];
            _loc5_ = _loc3_[1];
            this.AddMakeup(RaceMenuDefines.ENTRY_TYPE_WARPAINT,this.makeupList[RaceMenuDefines.PAINT_WAR],_loc6_,_loc5_);
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddBodyPaints()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "" && _loc3_[1] != "")
         {
            _loc6_ = _loc3_[0];
            _loc5_ = _loc3_[1];
            this.AddMakeup(RaceMenuDefines.ENTRY_TYPE_BODYPAINT,this.makeupList[RaceMenuDefines.PAINT_BODY],_loc6_,_loc5_);
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddHandPaints()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "" && _loc3_[1] != "")
         {
            _loc6_ = _loc3_[0];
            _loc5_ = _loc3_[1];
            this.AddMakeup(RaceMenuDefines.ENTRY_TYPE_HANDPAINT,this.makeupList[RaceMenuDefines.PAINT_HAND],_loc6_,_loc5_);
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddFeetPaints()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "" && _loc3_[1] != "")
         {
            _loc6_ = _loc3_[0];
            _loc5_ = _loc3_[1];
            this.AddMakeup(RaceMenuDefines.ENTRY_TYPE_FEETPAINT,this.makeupList[RaceMenuDefines.PAINT_FEET],_loc6_,_loc5_);
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddFacePaints()
   {
      var _loc4_ = 0;
      var _loc3_;
      var _loc6_;
      var _loc5_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_[0] != "" && _loc3_[1] != "")
         {
            _loc6_ = _loc3_[0];
            _loc5_ = _loc3_[1];
            this.AddMakeup(RaceMenuDefines.ENTRY_TYPE_FACEPAINT,this.makeupList[RaceMenuDefines.PAINT_FACE],_loc6_,_loc5_);
         }
         _loc4_ = _loc4_ + 1;
      }
   }
   function RSM_AddTints()
   {
      var _loc7_ = new Array();
      var _loc6_ = new Array();
      var _loc8_ = new Array();
      _global.tintCount = 0;
      var _loc5_ = 0;
      var _loc4_;
      while(_loc5_ < arguments.length)
      {
         _loc4_ = arguments[_loc5_].split(";;");
         if(_loc4_.length > 0 && (Number(_loc4_[0]) != 0 || Number(_loc4_[1]) != 0 || _loc4_[2] != ""))
         {
            _loc7_.push(Number(_loc4_[0]));
            _loc6_.push(Number(_loc4_[1]));
            _loc8_.push(_loc4_[2]);
            if(Number(_loc4_[0]) != RaceMenuDefines.TINT_TYPE_HAIR && Number(_loc4_[1]) >>> 24 > 0)
            {
               _global.tintCount = _global.tintCount + 1;
            }
         }
         _loc5_ = _loc5_ + 1;
      }
      this.UpdateTintCount();
      this.SetSliderColors(_loc7_,_loc6_);
      var _loc9_ = RaceMenuDefines.TINT_TYPE_WARPAINT;
      var _loc11_ = RaceMenuDefines.CATEGORY_WARPAINT;
      var _loc10_ = RaceMenuDefines.PAINT_WAR;
      var _loc12_ = RaceMenuDefines.ENTRY_TYPE_WARPAINT;
      this.SetMakeup(_loc7_,_loc6_,_loc8_,null,_loc9_,_loc11_,_loc10_,_loc12_);
      false;
      false;
      false;
   }
   function RSM_AddBodyTints()
   {
      var _loc8_ = new Array();
      var _loc6_ = new Array();
      var _loc5_ = new Array();
      var _loc7_ = new Array();
      var _loc4_ = 0;
      var _loc3_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_.length > 0 && (Number(_loc3_[0]) != 0 || Number(_loc3_[1]) != 0 || _loc3_[2] != ""))
         {
            _loc8_.push(Number(_loc3_[0]));
            _loc6_.push(Number(_loc3_[1]));
            _loc5_.push(_loc3_[2]);
            _loc7_.push(Number(_loc3_[3]));
         }
         _loc4_ = _loc4_ + 1;
      }
      var _loc9_ = RaceMenuDefines.TINT_TYPE_BODYPAINT;
      var _loc11_ = RaceMenuDefines.CATEGORY_BODYPAINT;
      var _loc10_ = RaceMenuDefines.PAINT_BODY;
      var _loc12_ = RaceMenuDefines.ENTRY_TYPE_BODYPAINT;
      this.SetMakeup(_loc8_,_loc6_,_loc5_,_loc7_,_loc9_,_loc11_,_loc10_,_loc12_);
      false;
      false;
      false;
      false;
   }
   function RSM_AddHandTints()
   {
      var _loc8_ = new Array();
      var _loc6_ = new Array();
      var _loc5_ = new Array();
      var _loc7_ = new Array();
      var _loc4_ = 0;
      var _loc3_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_.length > 0 && (Number(_loc3_[0]) != 0 || Number(_loc3_[1]) != 0 || _loc3_[2] != ""))
         {
            _loc8_.push(Number(_loc3_[0]));
            _loc6_.push(Number(_loc3_[1]));
            _loc5_.push(_loc3_[2]);
            _loc7_.push(Number(_loc3_[3]));
         }
         _loc4_ = _loc4_ + 1;
      }
      var _loc9_ = RaceMenuDefines.TINT_TYPE_HANDPAINT;
      var _loc11_ = RaceMenuDefines.CATEGORY_HANDPAINT;
      var _loc10_ = RaceMenuDefines.PAINT_HAND;
      var _loc12_ = RaceMenuDefines.ENTRY_TYPE_HANDPAINT;
      this.SetMakeup(_loc8_,_loc6_,_loc5_,_loc7_,_loc9_,_loc11_,_loc10_,_loc12_);
      false;
      false;
      false;
      false;
   }
   function RSM_AddFeetTints()
   {
      var _loc8_ = new Array();
      var _loc6_ = new Array();
      var _loc5_ = new Array();
      var _loc7_ = new Array();
      var _loc4_ = 0;
      var _loc3_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_.length > 0 && (Number(_loc3_[0]) != 0 || Number(_loc3_[1]) != 0 || _loc3_[2] != ""))
         {
            _loc8_.push(Number(_loc3_[0]));
            _loc6_.push(Number(_loc3_[1]));
            _loc5_.push(_loc3_[2]);
            _loc7_.push(Number(_loc3_[3]));
         }
         _loc4_ = _loc4_ + 1;
      }
      var _loc9_ = RaceMenuDefines.TINT_TYPE_FEETPAINT;
      var _loc11_ = RaceMenuDefines.CATEGORY_FEETPAINT;
      var _loc10_ = RaceMenuDefines.PAINT_FEET;
      var _loc12_ = RaceMenuDefines.ENTRY_TYPE_FEETPAINT;
      this.SetMakeup(_loc8_,_loc6_,_loc5_,_loc7_,_loc9_,_loc11_,_loc10_,_loc12_);
      false;
      false;
      false;
      false;
   }
   function RSM_AddFaceTints()
   {
      var _loc8_ = new Array();
      var _loc6_ = new Array();
      var _loc5_ = new Array();
      var _loc7_ = new Array();
      var _loc4_ = 0;
      var _loc3_;
      while(_loc4_ < arguments.length)
      {
         _loc3_ = arguments[_loc4_].split(";;");
         if(_loc3_.length > 0 && (Number(_loc3_[0]) != 0 || Number(_loc3_[1]) != 0 || _loc3_[2] != ""))
         {
            _loc8_.push(Number(_loc3_[0]));
            _loc6_.push(Number(_loc3_[1]));
            _loc5_.push(_loc3_[2]);
            _loc7_.push(Number(_loc3_[3]));
         }
         _loc4_ = _loc4_ + 1;
      }
      var _loc9_ = RaceMenuDefines.TINT_TYPE_FACEPAINT;
      var _loc11_ = RaceMenuDefines.CATEGORY_FACEPAINT;
      var _loc10_ = RaceMenuDefines.PAINT_FACE;
      var _loc12_ = RaceMenuDefines.ENTRY_TYPE_FACEPAINT;
      this.SetMakeup(_loc8_,_loc6_,_loc5_,_loc7_,_loc9_,_loc11_,_loc10_,_loc12_);
      false;
      false;
      false;
      false;
   }
   function RSM_ExtendRace(a_object)
   {
      if(a_object.formId != undefined)
      {
         skse.ExtendForm(a_object.formId,a_object,true,false);
      }
      this._raceList.push(a_object);
   }
   function RSM_SaveClipboard()
   {
      var _loc3_ = "";
      var _loc2_ = 0;
      while(_loc2_ < arguments.length)
      {
         _loc3_ += "" + (arguments[_loc2_] * 100 | 0) / 100 + "";
         if(_loc2_ < arguments.length - 1)
         {
            _loc3_ += ",";
         }
         _loc2_ = _loc2_ + 1;
      }
      skse.SetClipboardData(_loc3_);
   }
   function RSM_LoadClipboard()
   {
      var _loc4_ = skse.GetClipboardData();
      var _loc3_ = _loc4_.split(",");
      var _loc2_ = 0;
      while(_loc2_ < _loc3_.length)
      {
         skse.SendModEvent(_global.eventPrefix + "ClipboardData",Number(_loc2_).toString(),Number(_loc3_[_loc2_]));
         _loc2_ = _loc2_ + 1;
      }
      skse.SendModEvent(_global.eventPrefix + "ClipboardFinished");
   }
   function RSM_ToggleLoader(a_toggle)
   {
      this.loadingIcon._visible = a_toggle;
   }
   function RSM_SetPresetSlot(a_slot)
   {
      _global.presetSlot = a_slot;
   }
}
