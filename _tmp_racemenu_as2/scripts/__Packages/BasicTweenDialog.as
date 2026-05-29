class BasicTweenDialog extends MovieClip
{
   var dispatchEvent;
   var onDialogClosed;
   var onDialogClosing;
   var onDialogOpen;
   var onDialogOpening;
   var removeAllEventListeners;
   static var OPEN = 0;
   static var CLOSED = 1;
   static var OPENING = 2;
   static var CLOSING = 3;
   var _dialogState = -1;
   function BasicTweenDialog()
   {
      super();
      com.greensock.plugins.TweenPlugin.activate([com.greensock.plugins.AutoAlphaPlugin]);
      gfx.events.EventDispatcher.initialize(this);
      Mouse.addListener(this);
   }
   function openDialog()
   {
      this.setDialogState(BasicTweenDialog.OPENING);
      com.greensock.TweenLite.from(this,0.4,{autoAlpha:0,overwrite:"all",easing:com.greensock.easing.Linear.easeNone,onComplete:this.fadedInFunc,onCompleteParams:[this]});
   }
   function closeDialog()
   {
      this.setDialogState(BasicTweenDialog.CLOSING);
      com.greensock.TweenLite.to(this,0.4,{autoAlpha:0,overwrite:"all",easing:com.greensock.easing.Linear.easeNone,onComplete:this.fadedOutFunc,onCompleteParams:[this]});
   }
   function setDialogState(a_newState)
   {
      if(this._dialogState == a_newState)
      {
         return undefined;
      }
      this._dialogState = a_newState;
      if(a_newState == BasicTweenDialog.OPENING)
      {
         if(this.onDialogOpening)
         {
            this.onDialogOpening();
         }
         this.dispatchEvent({type:"dialogOpening"});
      }
      else if(a_newState == BasicTweenDialog.OPEN)
      {
         if(this.onDialogOpen)
         {
            this.onDialogOpen();
         }
         this.dispatchEvent({type:"dialogOpen"});
      }
      else if(a_newState == BasicTweenDialog.CLOSING)
      {
         if(this.onDialogClosing)
         {
            this.onDialogClosing();
         }
         this.dispatchEvent({type:"dialogClosing"});
      }
      else if(a_newState == BasicTweenDialog.CLOSED)
      {
         if(this.onDialogClosed)
         {
            this.onDialogClosed();
         }
         this.dispatchEvent({type:"dialogClosed"});
         this.removeAllEventListeners();
         this.removeMovieClip();
      }
   }
   function fadedInFunc(param)
   {
      param.setDialogState(BasicTweenDialog.OPEN);
   }
   function fadedOutFunc(param)
   {
      param.setDialogState(BasicTweenDialog.CLOSED);
   }
}
