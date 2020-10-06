
"""
"""
import logging
import json
from websocket import create_connection
import time
import decimal
import sys

class JSONWSException(Exception):
    def __init__(self, ws_error):
        Exception.__init__(self)
        self.error = ws_error

def EncodeDecimal(o):
    if isinstance(o, decimal.Decimal):
        return round(o, 8)
    raise TypeError(repr(o) + " is not JSON serializable")

def __func():
    return sys._getframe().f_back.f_code.co_name

log = logging.getLogger("HorizenWebsocket")

EVT_UPDATE_TIP = 0
EVT_UNDEFINED = 0xff

REQ_GET_SINGLE_BLOCK = 0
REQ_GET_MULTIPLE_BLOCK_HASHES = 1
REQ_GET_NEW_BLOCK_HASHES = 2
REQ_SEND_CERTIFICATE = 3
REQ_UNDEFINED = 0xff
    
MSG_EVENT = 0
MSG_REQUEST = 1
MSG_RESPONSE = 2
MSG_ERROR = 3
MSG_UNDEFINED = 0xff

#----------------------------------------------------------------
def fill_ws_send_certificate_input(args):
    if len(args) <6:
        raise JSONWSException("{}(): wrong number of args {}".format(__func(), len(args)))

    msg = {}
    msg['msgType']     = MSG_REQUEST
    msg['requestId']   = "req_" + str(time.time())
    msg['requestType'] = REQ_SEND_CERTIFICATE 

    msg['requestPayload'] = {}
    msg['requestPayload']['scid']              = args[0] # scid
    msg['requestPayload']['epochNumber']       = args[1] # epoch_number
    msg['requestPayload']['quality']           = args[2] # quality
    msg['requestPayload']['endEpochBlockHash'] = args[3] # epoch_block_hash
    msg['requestPayload']['scProof']           = args[4] # proof
    msg['requestPayload']['backwardTransfers'] = args[5] # bwt

    # optional
    if len(args) > 6:
        msg['requestPayload']['fee']           = args[6] # fee
 
    return json.dumps(msg, default=EncodeDecimal)

def fill_ws_send_certificate_output(jrsp):
    print "Json Received '%s'" % jrsp
    return jrsp['responsePayload']['certificateHash']

#----------------------------------------------------------------
def fill_ws_get_single_block_input(args):
    if len(args) == 0:
        raise JSONWSException("{}(): wrong number of args {}".format(__func(), len(args)))

    msg = {}
    msg['msgType']     = MSG_REQUEST
    msg['requestId']   = "req_" + str(time.time())
    msg['requestType'] = REQ_GET_SINGLE_BLOCK 

    msg['requestPayload'] = {}
    # TODO height or hash depending on arg value
    msg['requestPayload']['height'] = args[0] 
    return json.dumps(msg, default=EncodeDecimal)

def fill_ws_get_single_block_output(jrsp):
    print "Json Received '%s'" % jrsp
    return jrsp['responsePayload']['height'], jrsp['responsePayload']['hash'], jrsp['responsePayload']['block']

#----------------------------------------------------------------
# to be continued with all the supported messages...


# for negative tests 
#----------------------------------------------------------------
def fill_ws_test_input(args):
    if len(args) == 0:
        raise JSONWSException("{}(): wrong number of args {}".format(__func(), len(args)))
    msg = {}
    msg['msgType']     = 100
    msg['requestId']   = "req_" + str(time.time())
    msg['requestType'] = REQ_GET_SINGLE_BLOCK 

    msg['requestPayload'] = {}
    # TODO height or hash depending on arg value
    msg['requestPayload']['height'] = args[0] 
    '''
    msg = "qqq"
    '''
    return json.dumps(msg, default=EncodeDecimal)

def fill_ws_test_output(jrsp):
    print "Json Received '%s'" % jrsp
    return jrsp['responsePayload']['height'], jrsp['responsePayload']['hash'], jrsp['responsePayload']['block']


#----------------------------------------------------------------
def fill_ws_cmd_input(method, args):
    if method == "ws_send_certificate": return fill_ws_send_certificate_input(args)
    if method == "ws_get_single_block": return fill_ws_get_single_block_input(args)

    if method == "ws_test": return fill_ws_test_input(args)
    # add specific method calls here

    raise JSONWSException("Websocket method \"{}\" not supported".format(method))


def fill_ws_cmd_output(method, jrsp):

    if method == "ws_send_certificate": return fill_ws_send_certificate_output(jrsp)
    if method == "ws_get_single_block": return fill_ws_get_single_block_output(jrsp)

    if method == "ws_test": return fill_ws_test_output(jrsp)
    # add specific method calls here


    raise JSONWSException("Websocket method \"{}\" not supported".format(method))


class WsServiceProxy(object):
    __id_count = 0

    def ws_cmd(self, method, args, ws):
        WsServiceProxy.__id_count += 1

        json_data = fill_ws_cmd_input(method, args)

        print "################## Sending ######################"
        log.debug("-%s-> %s %s (ws msg send)"%(WsServiceProxy.__id_count, method,
                                 json.dumps(args, default=EncodeDecimal)))
        ws.send(json_data)
        print "Sent! Receiving..."
        resp =  ws.recv()
        print "Received '%s'" % resp
 
        jrsp = json.loads(resp)
        log.debug("-%s-> %s %s (ws response got)"%(WsServiceProxy.__id_count, method,
                                 json.dumps(jrsp, default=EncodeDecimal)))

        self._trap_ws_errors(method, jrsp)

        return fill_ws_cmd_output(method, jrsp)
 
    def get_wsurl(self):
        return self.__ws_url

    def __init__(self, ws_url=None, service_name=None,):
        self.__ws_url = ws_url
        self.__service_name = service_name


    def __getattr__(self, name):
        if name.startswith('__') and name.endswith('__'):
            # Python internal stuff
            raise AttributeError
        if self.__service_name is not None:
            name = "%s.%s" % (self.__service_name, name)
        return WsServiceProxy(self.__ws_url, name)


    def _request(self, method, args):
        if self.__ws_url is None:
            raise JSONWSException("No Websocket URL (zend -websocket=1 has been set?")

        print "##### connecting to ws_url {} ######################".format(self.__ws_url)
        print "##### ws method:", method
        print "##### data:", args

        ws = create_connection(self.__ws_url)
        try:
            resp = self.ws_cmd(method, args, ws)
        except JSONWSException, e:
            raise
        except Exception, e:
            ws.close()
            print "hhhhhhhhhhhhhhhhhhh ", str(e)
            raise JSONWSException("Exception got invoking WS req [{}] at [{}]".format(method, self.__ws_url))
        
        ws.close()
        return resp

    def _trap_ws_errors(self, method, jrsp):
        # check we have a well formed response
        if (jrsp['msgType'] is None):
            log.debug("-%s-> %s %s)"%(WsServiceProxy.__id_count, method, json.dumps(jrsp, default=EncodeDecimal)))
            raise JSONWSException("Ill formed response got from [{}]: {}".format(self.__ws_url, jrsp))
    
    
        if ((jrsp['msgType'] is not None) and (jrsp['msgType'] == MSG_ERROR)):
            try:
                if jrsp['requestId'] is not None:
                    # reqId is filled only if the sender has specified it
                    reqId   = jrsp['requestId']
                code    = jrsp['errorCode']
                message = jrsp['message']
            except:
                log.debug("-%s-> %s %s)"%(WsServiceProxy.__id_count, method, json.dumps(jrsp, default=EncodeDecimal)))
                raise JSONWSException("Ill formed response got from [{}]: {}".format(self.__ws_url, jrsp))
    
            raise JSONWSException("{}".format(message))

