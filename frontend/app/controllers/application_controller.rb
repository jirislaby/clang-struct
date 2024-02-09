class ApplicationController < ActionController::Base
  helper ApplicationHelper

  def listing_limit
    return 200
  end
end
