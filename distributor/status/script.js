var statusNames = {
    'working': 'Computing',
    'missing': 'Offline',
    'notresponding': 'Not responding',
    'extendedidle': 'Does not compute',
    'online': 'Ready'
};
var nodestatuses = 'working missing notresponding extendedidle online';

var nodes = {}, workunits = {}, nodecount = 0, threadcount = 0;
var noderoom = {}, rooms = {};

var sseObj = undefined;
var activepanel = 'listing';
var clockSkew = 0;

function time() {
    return (new Date().getTime()+clockSkew)/1000;
}

function expired(t) {
    return parseInt(t)+600 < time();
}

function nodeid(id) {
    if ( !id ) return '';
    var x = id.lastIndexOf('-');
    if ( x >= 0 ) id = id.substring(x+1);
    return id;
}

function nodestatus(id) {
    id = nodeid(id);
    // Node does not exist
    if ( !id || !nodes[id] ) return 'missing';
    // Not Responding to pings (Shouldn't see this with SSE; don't have updated
    // info)
    if ( !sseObj && expired(nodes[id]['seen']) ) return 'notresponding';
    // Node has not returned any successful work in a long time.
    if ( expired(nodes[id]['seenwork']) ) return 'extendedidle';
    // Online, ready to work, but not at present.
    if ( sseObj && nodes[id]['assigned'] == 0 ) return 'online';
    // Actually working right now (or, without SSE, worked in last few minutes)
    return 'working';
}

function showPanel(p) {
    $('.panel').hide();
    $('#' + p).show();
    $('#nav A').removeClass('active');
    $('#showpanel-' + p).addClass('active');
    activepanel = p;
    updateStatusAll();
    plotStop();
}

// Check that the node's existance in the list matches the existance in the UI
function checkNode(id) {
    var status = nodestatus(id);
    var found = $('#listing-'+id);
    if ( status != 'missing' && !found.length ) {
        var node = nodes[id];
        var html = '<tr class=noderow id="listing-' + id +
            '"><td class="col-name">' + node['name'] +
            '</td><td class="col-threads">' + node['threads'] +
            '</td><td class="col-status node">' + statusNames[status]
            +'</td></tr>';
        $('#listing TBODY').append(html);
        nodecount++;
        // Update node count
        $('.nodecount').text(nodecount);
    }
    else if ( found.length && status == 'missing' ) {
        found.remove();
        nodecount--;
        // Update node count
        $('.nodecount').text(nodecount);
    }
}

function cleanupNodes() {
    // Remove all excess nodes from table
    $('#listing TBODY TR').filter(function () {
        if ( nodestatus($(this).attr('id')) == 'missing' )
            { nodecount--; return true; }
        return false;
    }).remove();
}

// Auto-update (restart timer and do update)
function autoUpdate() {
    // Update soon
    setTimeout(function () { autoUpdate() }, 600*1000);
    update();
}

// Do a manual update (or if SSE is working, an expiration check)
function update() {
    // Check if SSE is working
    if ( sseObj ) return updateStatusAll();
    // Request update
    $.ajax({url: 'workers.json', dataType: 'json', cache: false,
            success: function (n, status, jqXHR) {
        // Check clock skew
        var servernow = Date.parse(jqXHR.getResponseHeader('Date'));
        if ( !isNaN(servernow) ) gotTimeReference(servernow);
        // Update data structures
        nodes = n;
        workunits = {};
        $('#headconnection *').hide();
        $('#connection-plain, #connection-connect').show();
        // Add nodes to table
        for ( i in nodes ) checkNode(i);
        // Remove missing nodes from table
        cleanupNodes();
        // Update visual status
        updateStatusAll();
        // Update node count
        $('.nodecount').text(nodecount);
        // Update thread count
        threadcount = 0;
        for ( i in nodes ) threadcount += parseInt(nodes[i]['threads']);
        $('.threadcount').text(threadcount);
    }});
}

// Got a reference timestamp from server, use to update clock skew
function gotTimeReference(t) {
    clockSkew = t-new Date().getTime();
}

// Update the status display of each node
function updateStatusAll() {
    if ( activepanel == 'listing' ) { // Update table
        $('#listing TBODY TR').each(updateListNode);
        // Update table sorting
        tableSortApply();
    }
    else // Update maps
        $('.map .node').each(updateMapNode);
}

function updateStatus(id) {
    if ( activepanel == 'listing' ) { // Update table
        $('#listing-'+id).each(updateListNode);
        // Update table sorting (FIXME: Necessary?)
        tableSortApply();
    }
    else // Update maps
        $('.map *[id$="-'+id+'"]').each(updateMapNode);
}

// Update maps. Note that SVG elements do not support className, so work with
// the class attribute directly
function updateMapNode() {
    var id = nodeid($(this).attr('id'));
    var status = nodestatus(id);
    $(this).attr('class','node '+status);
    //setGradient(id, 'fill', this)
}
// Update table row.
function updateListNode() {
    var cols = $(this).find('TD');
    var id = nodeid($(this).attr('id'));
    var status = nodestatus(id);
    $(cols[2]).attr('class', 'col-status node '+status);
    $(cols[2]).text(statusNames[status]);
    setGradient(id, 'background-image', cols[2])
    $(cols[1]).text(nodes[id]['threads']);
}

// If we are live, set the background of a node to a blinkenlights-type
// gradient representing the number of active threads.
var gradprefixes = ['-webkit-linear-gradient(left,',
                    '-moz-linear-gradient(left,',
                    '-ms-linear-gradient(left,',
                    '-o-linear-gradient(left,',
                    'linear-gradient(to right,'];
var usegradprefix = 0;
function setGradient(id, type, obj) {
    if ( usegradprefix >= gradprefixes.length ) return; // No gradient support
    if ( !sseObj || nodestatus(id) != 'working' ) {
        $(obj).css(type,'');
        return;
    }
    var percent = 100*nodes[id]['assigned']/nodes[id]['threads'];
    var c1 = '#0f0' /* working */, c2 = '#cfc' /* online */;
    var p1 = ' '+percent, p2 = ' '+(percent+0.1);
    var csstail = c1+','+c1+p1+'%,'+c2+p2+'%,'+c2+')';
    for ( ; usegradprefix < gradprefixes.length; usegradprefix++ ) {
        $(obj).css(type, gradprefixes[usegradprefix]+csstail);
        var rc = $(obj).css(type);  // If set works, then use this version
        if ( rc && rc != 'none' && rc != 'auto' ) return;
    }
}

// Handle incoming SSE messages
function sseMessage(msg, itemtype, id, obj) {
    if ( msg == 'DELETE' ) {
        if ( itemtype == 'WORKUNIT' ) {
            if ( workunits[id] && workunits[id]['worker'] ) {
                // We know about this workunit
                workerid = workunits[id]['worker'];
                if ( nodes[workerid] ) {
                    // Update the status of the worker working on the workunit
                    if ( obj && obj == 'FINISHED' ) {
                        // Successful completion: Update seenwork
                        nodes[workerid]['seenwork'] = time();
                    }
                    // Disassociate workunit from worker
                    if ( nodes[workerid]['assigned'] > 0 )
                        nodes[workerid]['assigned']--;
                    updateStatus(workerid);
                }
                if ( workunits[id]['starttime'] ) {
                    var duration = time()-workunits[id]['starttime'];
                    plotItem(parseInt(workunits[id]['nitems'])/duration,
                             duration, workerid);
                }
            }
            delete workunits[id];
        }
        else if ( itemtype == 'WORKER' ) {
            // Update thread count
            threadcount -= parseInt(nodes[id]['threads']);
            $('.threadcount').text(threadcount);
            // Delete worker
            delete nodes[id];
            checkNode(id);
            // Delete its workunits? (Server should inform us)
        }
    }
    else { // CREATE or UPDATE
        if ( itemtype == 'WORKUNIT' ) {
            workunits[id] = obj;
            if ( workunits[id] && workunits[id]['worker'] ) {
                // Workunit is associated with a worker
                workerid = workunits[id]['worker'];
                if ( nodes[workerid] ) {
                    if ( msg == 'CREATE' ) {
                        // Add workunit to worker
                        nodes[workerid]['assigned']++;
                        updateStatus(workerid);
                    }
                }
            }
        }
        else if ( itemtype == 'WORKER' ) {
            // Update thread count
            if ( nodes[id] && nodes[id]['threads'] )
                threadcount -= parseInt(nodes[id]['threads']);
            threadcount += parseInt(obj['threads']);
            $('.threadcount').text(threadcount);
            // Update worker
            nodes[id] = obj;
            checkNode(id);
            updateStatus(id);
        }
    }
}

// Try to establish an SSE connection
function sseConnect() {
    // Try to use Server-Sent Events (SSE) for live updating
    try {
        if ( window.EventSource ) {
            sseObj = new EventSource('monitor.cgi');
            // An SSE connection has been established
            sseObj.addEventListener('open', function () {
                nodes = {};
                workunits = {};
                cleanupNodes();
                threadcount = 0;
                update();
                clockSkew = 0;
                $('#workunitplot-container').show();
                // Update icon
                $('#headconnection *').hide();
                $('#connection-sse, #connection-disconnect').show();
            });
            // A message has arrived over the SSE connection
            sseObj.addEventListener('message', function(event) {
                function splitWithTail(str,delim,count){
                    // http://stackoverflow.com/a/5582719/462117
                    var parts = str.split(delim);
                    var tail = parts.slice(count).join(delim);
                    var result = parts.slice(0,count);
                    result.push(tail);
                    return result;
                }
                //console.log(event.origin+' '+event.data);
                // Broken on Firefox when using IPv6
                //     event.origin == "http://2620:9b::5d7:7692:9990"
                //     event.origin != "http://[2620:9b::5d7:7692]:9990"
                //if ( event.origin != location.protocol+"//"+location.host )
                //    return;
                data = splitWithTail(event.data, ' ', 3);
                if ( data[0] == 'DATE' ) gotTimeReference(parseInt(data[1])*1000);
                if ( data[2] )
                    sseMessage(data[0], data[1], data[2], data[0] == 'DELETE' ?
                               data[3] : JSON.parse(data[3]));
            });
            // An error has occured with the SSE connection
            sseObj.addEventListener('error', function() {
                // Update icon
                $('#headconnection *').hide();
                $('#connection-sse-error, #connection-disconnect').show();
                // If the browser is not trying to reconnect, give up on using
                // SSE and fall back to standard refreshing
                if ( sseObj && sseObj.readyState != 0 ) sseDisconnect();
            });
        }
    } catch (e) {
        sseObj = undefined;
    }
    if ( !sseObj )
        // Browser doesn't support SSE, don't offer to reconnect
        $('#connection-connect').remove();
}
// Handle a disconnection from SSE and switch to plain mode.
function sseDisconnect() {
    $('#headconnection *').hide();
    $('#connection-sse-plain, #connection-connect').show();
    plotStop();
    $('#workunitplot-container').hide();
    var x = sseObj;
    sseObj = undefined;
    x.close();
    update();
}

$(document).ready(function () {
    // Set up navigation bar
    $('.map').each(function () {
         var id = nodeid($(this).attr('id'));
         $('#nav').append($('<a>').attr('href','#'+id)
            .attr('id','showpanel-map-'+id).text(id));
    });
    $('#nav').append($('<a>').attr('href','#listing')
                .attr('id','showpanel-listing').text('All'))
             /*.append($('<a>').attr('href','#blinkenlights')
                .attr('id','showpanel-blinkenlights').text('Blinkenlights'))*/;
    // Set up event listeners for navigation bar
    $('#nav a').click(function () {
        showPanel($(this).attr('id').substring(10)); // Remove "showpanel-".
        return false;
    });
    $('#connection-connect').click(function () {
        $('#headconnection *').hide();
        $('#connection-sse-loading, #connection-disconnect').show();
        sseConnect();
        return false;
    });
    $('#connection-disconnect').click(function () {
        sseDisconnect();
        return false;
    });
    // Find all of the nodes that are in rooms
    $('.map .node').each(function () {
        var room = nodeid($(this).parent().parent().attr('id'));
        var node = nodeid($(this).attr('id'));
        noderoom[node] = room;
        if ( !rooms[room] ) rooms[room] = [];
        rooms[room][rooms[room].length] = node;
    });

    // Uses sortElements by James Padolsey, available under MIT license or GPL.
    // https://github.com/padolsey/jQuery-Plugins/tree/master/sortElements
    tableSortInit();

    $('#workunitplot-show').click(function() {plotStart();return false});
    $('#workunitplot-hide').click(function() {plotStop();return false});
    plotInit($('#workunitplot')[0]);
    // Switch to the listing panel
    showPanel('listing');

    sseConnect();
    autoUpdate();
});

// Table sorting
var sortable = '#listing';
var sortIndex = 0;
var sortDir = 0;    // 0 = Ascending (Up), 1 = Descending (Down)
function tableSortApply() {
    if ( activepanel != 'listing' ) return;
    // Update sort indicators
    $(sortable).find('th').removeClass('headerSortUp headerSortDown')
        .filter(function() { return $(this).index() == sortIndex; })
        .addClass(function () {
            return sortDir ? 'headerSortDown' : 'headerSortUp';
        });
    // Actually sort the table
    $(sortable).find('tbody').find('tr').sortElements(tableSortComparator);
}

function tableSortInit() {
    $(sortable).find('th').click(function () {
        if ( sortIndex == $(this).index() ) sortDir = sortDir ? 0 : 1;
        else { sortIndex = $(this).index(); sortDir = 0; }
        tableSortApply();
    });
    tableSortApply();
}

function tableSortComparator(a, b) {
    var arow = $(a).find('td'), brow = $(b).find('td');
    // Use one-based columns in this array to allow sign to denote direction.
    var cols = [ (sortIndex+1)*(sortDir ? -1 : 1), 1, 2 ];
    for ( i = 0; i < cols.length; i++ ) {
        var factor = cols[i] > 0 ? 1 : -1;
        var col = Math.abs(cols[i])-1;
        var at = $(arow[col]).text(), bt = $(brow[col]).text();
        if ( at < bt ) return -factor;
        if ( at > bt ) return factor;
    }
    return 0;
}

// Workunit plotting
var plotCanvas, plotCtx;
var plotTimescale = 10, plotYmax = 1;
var plotSlidetime = new Date();
var plotRunning = true;

var plotData = [];

function plotInit(canv) {
    plotCanvas = canv;
    plotCtx = plotCanvas.getContext('2d');
    plotStop();
    plotSlide();
}

function plotStart() {
    if ( !sseObj ) return;
    plotRunning = true;
    $('#workunitplot-show').hide(); $('#workunitplot-hide').show();
    plotCanvas.style.display='block';
    plotData = [];
    plotRedraw();
}
function plotStop() {
    plotRunning = false;
    $('#workunitplot-hide').hide(); $('#workunitplot-show').show();
    plotCanvas.style.display='none';
    plotData = [];
    plotRedraw();
}

function plotRedraw() {
    var now = plotSlidetime = new Date();
    if ( !plotRunning ) plotCtx.fillStyle = '#ccc';
    plotCtx.beginPath();
    plotCtx.rect(0, 0, plotCanvas.width, plotCanvas.height);
    plotCtx.fill();
    if ( !plotRunning ) {
        plotCtx.fillStyle = 'black';
        plotCtx.beginPath();
        var triangleSize = plotCanvas.height/8;
        plotCtx.moveTo(plotCanvas.width/2-triangleSize/2,
                       plotCanvas.height/2+triangleSize/2);
        plotCtx.lineTo(plotCanvas.width/2-triangleSize/2,
                       plotCanvas.height/2-triangleSize/2);
        plotCtx.lineTo(plotCanvas.width/2+triangleSize/2, plotCanvas.height/2);
        plotCtx.fill();
        plotCtx.fillStyle = 'white';
        return;
    }
    for ( i in plotData ) {
        var offset = plotCanvas.width-Math.floor((now.getTime()-plotData[i][0])/1000*plotTimescale);
        var ypx = plotYtoPx(plotData[i][1]);
        plotCtx.beginPath();
        plotCtx.moveTo(offset, ypx);
        plotCtx.lineTo(offset-plotData[i][2]*plotTimescale, ypx);
        plotCtx.stroke();
    }
}

function plotSlide() {
    // Slide again soon
    setTimeout(plotSlide, 100);
    if ( !plotData.length ) {
        plotSlidetime = new Date();
        return;
    }
    // Slide canvas
    var now = new Date();
    var delta = Math.floor(plotTimescale*(now.getTime()-plotSlidetime.getTime())/1000);
    if ( delta > 0 ) {
        plotCtx.drawImage(plotCanvas, -delta, 0);
        // Blank new portion
        plotCtx.beginPath();
        plotCtx.rect(plotCanvas.width-delta, 0, delta, plotCanvas.height);
        plotCtx.fill();
        plotSlidetime = now;
    }
    // Remove old items from the plot history
    var xmin = now.getTime()-plotCanvas.width/plotTimescale*1000;
    while ( plotData.length && plotData[0][0].getTime() < xmin ) {
        plotData.splice(0, 1);
    }
}

function plotItem(y, duration, type) {
    if ( !plotRunning ) return;
    plotData.push([new Date(), y, duration, type]);
    if ( y > plotYmax ) {
        plotYmax = Math.floor(y)+1;
        plotRedraw();
    }
    else {
        var ypx = plotYtoPx(y);
        plotCtx.beginPath();
        plotCtx.moveTo(plotCanvas.width, ypx);
        plotCtx.lineTo(plotCanvas.width-duration*plotTimescale, ypx);
        plotCtx.stroke();
    }
}
function plotYtoPx(y) {
    return Math.floor((plotCanvas.height-1)*(1-y/plotYmax))+.5;
}
