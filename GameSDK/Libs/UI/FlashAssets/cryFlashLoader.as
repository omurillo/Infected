import cryflash.*;
import caurina.transitions.Tweener;

_global.gfxExtensions 	= true;
this.tabChildren 		= false;



var _ROOT:Main 							= new Main();


var listBoxCaptions	:Array				= new Array();
var listBoxValues	:Array 				= new Array();


var Cursor			:MovieClip			= new MovieClip();
var logoOn			:Boolean			= false;
//trace("Loading all Flash Menu Systems");

//{ REGION : Dublicate functions:
function cryInit					(sys):Void
{
	//trace("CryInit called - initialize controller");
	_ROOT.cryInit						(sys)};
///////////////////////////////////////////////////////////////////////////////////////////////////
function cry_onSetup				(sys):Void
{	//trace("cry_onSetup Called - initialize system configs");
	_ROOT.cry_onSetup				(sys);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function show_loader				():Void
{
	_ROOT.showLoader();
}
function hide_loader				():Void
{
	_ROOT.hideLoader();
}
function setBackgroundObject(obj):Void
{
	//Deprecated
}
function cry_onController				(iButton, bReleased):Void
{	//trace("cry_onController called - navigate menu");
	_ROOT.cry_onController				(iButton, bReleased);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function setupScreenSimple			(name):Void
{_ROOT.setupScreenSimple			(name);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSeparator				(caption, id, tooltip, column):Void
{_ROOT.addSeparator					(caption, id, tooltip, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addButtonSimple			(caption, tooltip, id, column):Void
{_ROOT.addButtonSimple				(caption, tooltip, id, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addTextFieldSimple			(caption, tooltip, id, column, isSending):Void
{_ROOT.addTextFieldSimple			(caption, tooltip, id, column, isSending);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSwitchSimple			(caption, tooltip, val, id, column):Void
{_ROOT.addSwitchSimple				(caption, tooltip, val, id, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSwitchOption			(id, caption, value):Void
{_ROOT.addSwitchOption				(id, caption, value); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSwitchOptionsFromString	(idx, options, delimit):Void
{_ROOT.addSwitchOptionsFromString	(idx, options, delimit); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSliderSimple			(caption, tooltip, min, max, step, val, id, column):Void
{_ROOT.addSliderSimple				(caption, tooltip, min, max, step, val, id, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addBackButton				(id):Void
{_ROOT.addBackButton				(id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addDefaultButton			(caption, tooltip, id):Void
{_ROOT.addDefaultButton				(caption, tooltip, id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addApplyButton				(caption, tooltip, id):Void
{_ROOT.addApplyButton				(caption, tooltip, id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addDefaultButton			(caption, tooltip, id):Void
{_ROOT.addDefaultButton				(caption, tooltip, id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addQuitButtonSimple		(caption, tooltip, id):Void
{_ROOT.addQuitButtonSimple			(caption, tooltip, id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addScreenText				(id, caption, tooltip, column):Void
{_ROOT.addScreenText				(id, caption, tooltip, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addListbox					(id, caption, tooltip, column):Void
{_ROOT.addListbox					(id, caption, tooltip, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addListboxItem				(id, caption, tooltip):Void
{
	//trace("Values of Item: " + id + "," + caption + "," + tooltip);
	_ROOT.addListboxItem				(id, caption, tooltip); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addListboxItems			(idx:String):Void
{
	_ROOT.addListboxItems				(idx); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function clearListbox				(id):Void
{_ROOT.clearListbox					(id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function setupLoadingLevel			(imgfile):Void
{_ROOT.setupLoadingLevel			(imgfile);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function setControlVal				(id, value):Void
{_ROOT.setControlVal				(id, value); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function getControlVal				(id):String
{return _ROOT.getControlVal			(id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function showConfirmDiag			(msg, btn1, btn2, id):Void
{_ROOT.showConfirmDiag				(msg, btn1, btn2, id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function hideConfirmDiag			():Void
{_ROOT.hideConfirmDiag				();}
///////////////////////////////////////////////////////////////////////////////////////////////////
function setVersion					(ver):Void
{_ROOT.setVersion					(ver); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function setTextFieldText			(id, value):Void
{_ROOT.setTextFieldText				(id, value); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function getTextFieldText			(id):String
{return _ROOT.getTextFieldText		(id); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function showParticles				():Void
{_ROOT.showParticles				(); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function hideParticles				():Void
{_ROOT.hideParticles				(); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function showSpinner				():Void
{_ROOT.showSpinner					(); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function hideSpinner				():Void
{_ROOT.hideSpinner					(); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function clearSwitchOptions			(id):Void
{_ROOT.clearSwitchOptions			(id); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addChatSimple				(caption, tooltip, id, column):Void
{_ROOT.addChatSimple				(caption, tooltip, id, column); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function sendMessage				(id, msg):Void
{_ROOT.sendMessage					(id, msg);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addImage					($path, $id, $x, $y, $width, $height):Void
{_ROOT.addImage						($path, $id, $x, $y, $width, $height);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function hideImage					(id):Void
{_ROOT.hideImage					(id); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function showImage					(id):Void
{_ROOT.showImage					(id);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addItemSelect				(id, caption, tooltip, column):Void
{_ROOT.addItemSelect				(id, caption, tooltip, column);}
///////////////////////////////////////////////////////////////////////////////////////////////////
function addLevelButton				(caption, tooltip, id, column):Void
{_ROOT.addLevelButton				(caption, tooltip, id, column); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addSelectItem				(id, caption, value):Void
{_ROOT.addSelectItem				(id, caption, value); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function setupLoadingLevel			(path):Void
{_ROOT.setupLoadingLevel			(path); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function addLevelButton				(caption, tooltip, id, column):Void
{_ROOT.addLevelButton				(caption, tooltip, id, column); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function storeTempVal				(id:String, value):Void
{_ROOT.storeTempVal					(id, value); }
///////////////////////////////////////////////////////////////////////////////////////////////////
function getTempVal					(id):String
{return _ROOT.getTempVal			(id); }

function removeButton(id):Void
{
	_ROOT.removeButton(id);	
}
function hideBG():Void
{
	_ROOT.hideBG();
}
function showBG():Void
{
	_ROOT.showBG();
}
///////////////////////////////////////////////////////////////////////////////////////////////////
//}
function showlogo()
{
	Tweener.addTween(CryRegistry.CryStaticElements.inLogoLogo, { _alpha: 100, time:1 });
	CryRegistry.CryPageName._alpha = 0;
	Tweener.addTween(_root.swing, {_width:(Stage.width / 2), time:1 } );
	logoOn = true;
}
function hidelogo()
{
	Tweener.addTween(CryRegistry.CryStaticElements.inLogoLogo, { _alpha:0, time:1 });
	CryRegistry.CryPageName._alpha = 100;
	Tweener.addTween(_root.swing, {_width:(Stage.width * 0.80), time:1 } );	
	logoOn = false;
}

